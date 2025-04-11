#include "pocketflow.h"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::make_shared

// Use the namespace
using namespace pocketflow;

// --- Test Node Implementations ---

// Define a custom start node
// Template arguments: P=void (no prep input), E=std::string (exec returns string)
class MyStartNode : public Node<std::nullptr_t, std::string> {
public:
    // Override exec (pure virtual in BaseNode)
    std::string exec(std::nullptr_t /*prepResult*/) override {
        std::cout << "Starting workflow..." << std::endl;
        return "started";
    }
    // Override post (optional)
     std::optional<std::string> post(Context& ctx, const std::nullptr_t& p, const std::string& e) override {
         // Return the exec result directly as the action
         return e;
     }
};

// Define a custom end node
// Template arguments: P=std::string (prep returns string), E=void (exec returns nothing)
class MyEndNode : public Node<std::string, std::nullptr_t> {
public:
    // Override prep (optional)
    std::string prep(Context& ctx) override {
        // Example: Read something from context if needed
        // return std::any_cast<std::string>(ctx.at("some_key"));
        return "Preparing to end workflow";
    }

    // Override exec
    std::nullptr_t exec(std::string prepResult) override {
        std::cout << "Ending workflow with: " << prepResult << std::endl;
        // Since E is void (represented by nullptr_t), we don't return anything meaningful
        return nullptr;
    }

    // Override post (optional, default returns nullopt/default action)
    std::optional<std::string> post(Context& ctx, const std::string& p, const std::nullptr_t& e) override {
        ctx["end_node_prep_result"] = p; // Example: Store something in context
        return std::nullopt; // No further action needed
    }
};


// --- Example Test Nodes mirroring Java Test ---

// P=nullptr_t, E=int
class SetNumberNode : public Node<std::nullptr_t, int> {
    int number;
public:
    SetNumberNode(int num) : number(num) {}

    int exec(std::nullptr_t) override {
        int multiplier = getParamOrDefault<int>("multiplier", 1);
        return number * multiplier;
    }

    std::optional<std::string> post(Context& ctx, const std::nullptr_t&, const int& e) override {
        ctx["currentValue"] = e; // Store result in context
        return e > 20 ? std::make_optional("over_20") : std::nullopt; // Branching action
    }
};

// P=int, E=int
class AddNumberNode : public Node<int, int> {
    int numberToAdd;
public:
    AddNumberNode(int num) : numberToAdd(num) {}

    int prep(Context& ctx) override {
        // Get value from context, throw if not found or wrong type
        try {
            return std::any_cast<int>(ctx.at("currentValue"));
        } catch (const std::out_of_range& oor) {
            throw PocketFlowException("Context missing 'currentValue' for AddNumberNode");
        } catch (const std::bad_any_cast& bac) {
             throw PocketFlowException("'currentValue' in context is not an int for AddNumberNode");
        }
    }

    int exec(int currentValue) override {
        return currentValue + numberToAdd;
    }

    std::optional<std::string> post(Context& ctx, const int&, const int& e) override {
        ctx["currentValue"] = e; // Update context
        return "added";          // Fixed action
    }
};

// P=int, E=nullptr_t
class ResultCaptureNode : public Node<int, std::nullptr_t> {
public:
    int capturedValue = -999; // Store result locally for testing

    int prep(Context& ctx) override {
        // Get value from context, provide default if missing
         auto it = ctx.find("currentValue");
         if (it != ctx.end()) {
             try {
                 return std::any_cast<int>(it->second);
             } catch(const std::bad_any_cast& ) {
                 // Handle error or return default
             }
         }
         return -999; // Default if not found or bad cast
    }

    std::nullptr_t exec(int prepResult) override {
        capturedValue = prepResult; // Capture the value
        // Also store in params map like the Java example
        this->params["capturedValue"] = prepResult;
        return nullptr;
    }
    // No post needed, default action (nullopt) is fine
};


int main() {
    // --- Simple Workflow Example ---
    std::cout << "--- Running Simple Workflow ---" << std::endl;
    auto startNode = std::make_shared<MyStartNode>();
    auto endNode = std::make_shared<MyEndNode>();

    // Connect the nodes: startNode transitions to endNode on the "started" action
    startNode->next(endNode, "started");

    // Create a flow with the start node
    Flow flow(startNode);

    // Create a context and run the flow
    Context context;
    std::cout << "Executing workflow..." << std::endl;
    flow.run(context); // Returns the final action, ignored here
    std::cout << "Workflow completed successfully." << std::endl;
    // Check context if endNode modified it
    if (context.count("end_node_prep_result")) {
         std::cout << "End node stored in context: "
                   << std::any_cast<std::string>(context["end_node_prep_result"])
                   << std::endl;
    }
    std::cout << std::endl;


    // --- Linear Flow Test Example (like Java test) ---
     std::cout << "--- Running Linear Test Workflow ---" << std::endl;
     auto setNum = std::make_shared<SetNumberNode>(10);
     auto addNum = std::make_shared<AddNumberNode>(5);
     auto capture = std::make_shared<ResultCaptureNode>();

     setNum->next(addNum) // Default action connects to addNum
          ->next(capture, "added"); // addNum's "added" action connects to capture

     Flow linearFlow(setNum);
     Context linearContext;
     linearFlow.run(linearContext);

     // Assertions (manual checks here)
     std::cout << "Linear Test: Final Context 'currentValue': "
               << (linearContext.count("currentValue") ? std::any_cast<int>(linearContext["currentValue"]) : -1)
               << " (Expected: 15)" << std::endl;
     std::cout << "Linear Test: Captured Value in Node: "
               << capture->capturedValue
               << " (Expected: 15)" << std::endl;
     // Check params map in capture node
     if (capture->getParams().count("capturedValue")) {
         std::cout << "Linear Test: Captured Value in Params: "
                   << std::any_cast<int>(capture->getParams().at("capturedValue"))
                   << " (Expected: 15)" << std::endl;
     }
     std::cout << std::endl;


    // --- Branching Flow Test Example ---
    std::cout << "--- Running Branching Test Workflow ---" << std::endl;
    auto setNumBranch = std::make_shared<SetNumberNode>(10); // Multiplier will make it > 20
    auto addNumBranch = std::make_shared<AddNumberNode>(5);
    auto captureDefault = std::make_shared<ResultCaptureNode>();
    auto captureOver20 = std::make_shared<ResultCaptureNode>();

    // Connections
    setNumBranch->next(addNumBranch);                             // Default action
    setNumBranch->next(captureOver20, "over_20");                 // "over_20" action
    addNumBranch->next(captureDefault, "added");                  // "added" action from addNum

    Flow branchingFlow(setNumBranch);
    Context branchingContext;
    // Set params for the flow (which get passed to the first node)
    branchingFlow.setParams({{"multiplier", 3}}); // Make initial value 30 (> 20)

    branchingFlow.run(branchingContext);

    // Assertions (manual checks)
     std::cout << "Branching Test: Final Context 'currentValue': "
               << (branchingContext.count("currentValue") ? std::any_cast<int>(branchingContext["currentValue"]) : -1)
               << " (Expected: 30)" << std::endl; // From SetNumberNode's post
     std::cout << "Branching Test: Default Capture Node Value: "
               << captureDefault->capturedValue
               << " (Expected: -999 - not executed)" << std::endl;
     std::cout << "Branching Test: Over_20 Capture Node Value: "
               << captureOver20->capturedValue
               << " (Expected: 30)" << std::endl;
     std::cout << std::endl;


    return 0;
}