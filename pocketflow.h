#ifndef POCKETFLOW_H
#define POCKETFLOW_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::shared_ptr
#include <any>    // For type erasure (like Java Object) - C++17
#include <optional> // For optional return values (like Java null) - C++17
#include <stdexcept>
#include <chrono>
#include <thread>
#include <functional> // For std::function if needed (not strictly used here yet)
#include <utility> // For std::move

namespace pocketflow {

// --- Type Definitions ---
using Context = std::map<std::string, std::any>;
using Params = std::map<std::string, std::any>;

// --- Constants ---
// Use std::nullopt to represent the default action instead of a magic string
// static const std::string DEFAULT_ACTION = "default"; // Optional: if you prefer explicit string

// --- Utility Functions ---
inline void logWarn(const std::string& message) {
    std::cerr << "WARN: PocketFlow - " << message << std::endl;
}

// --- Custom Exception ---
class PocketFlowException : public std::runtime_error {
public:
    PocketFlowException(const std::string& message) : std::runtime_error(message) {}
    PocketFlowException(const std::string& message, const std::exception& cause)
        : std::runtime_error(message + " (Caused by: " + cause.what() + ")") {} // Simple cause handling
};


// --- Forward Declarations ---
class IBaseNode; // Non-templated base interface

// --- Base Node Interface (Non-Templated) ---
// Needed to store heterogeneous node types in successors map
class IBaseNode {
public:
    virtual ~IBaseNode() = default; // IMPORTANT: Virtual destructor

    virtual void setParamsInternal(const Params& params) = 0;
    virtual std::optional<std::string> internalRun(Context& sharedContext) = 0;
    virtual std::shared_ptr<IBaseNode> getNextNode(const std::optional<std::string>& action) const = 0;
    virtual bool hasSuccessors() const = 0;
    virtual const std::string& getClassName() const = 0; // For logging

    // Simplified next chaining accepting any IBaseNode. More type-safe versions
    // can be added in the templated BaseNode.
    virtual std::shared_ptr<IBaseNode> next(std::shared_ptr<IBaseNode> node, const std::string& action) = 0;
    virtual std::shared_ptr<IBaseNode> next(std::shared_ptr<IBaseNode> node) = 0; // Default action

    // Allow getting params (e.g., for result capture in tests)
    virtual const Params& getParams() const = 0;
};


// --- Base Node Template ---
template <typename P, typename E>
class BaseNode : public IBaseNode {
protected:
    Params params;
    std::map<std::string, std::shared_ptr<IBaseNode>> successors;
    std::string className = typeid(*this).name(); // Store class name approximation

public:
    virtual ~BaseNode() override = default;

    // --- Configuration ---
    // Returns *this reference to allow chaining like Java, but less common in C++
    BaseNode<P, E>& setParams(const Params& newParams) {
        params = newParams; // Creates a copy
        return *this;
    }

    // Override from IBaseNode
    void setParamsInternal(const Params& newParams) override {
        params = newParams;
    }

    const Params& getParams() const override {
        return params;
    }

    // --- Chaining ---
    // Templated next for potential type checking at connection time if needed,
    // but primarily delegates to the IBaseNode interface version.
    template <typename NEXT_P, typename NEXT_E>
    std::shared_ptr<BaseNode<NEXT_P, NEXT_E>> next(std::shared_ptr<BaseNode<NEXT_P, NEXT_E>> node, const std::string& action) {
        if (!node) {
            throw std::invalid_argument("Successor node cannot be null");
        }
        if (successors.count(action)) {
            logWarn("Overwriting successor for action '" + action + "' in node " + getClassName());
        }
        successors[action] = node; // Implicit cast to shared_ptr<IBaseNode>
        return node;
    }

    template <typename NEXT_P, typename NEXT_E>
    std::shared_ptr<BaseNode<NEXT_P, NEXT_E>> next(std::shared_ptr<BaseNode<NEXT_P, NEXT_E>> node) {
        // Use "" or a specific constant internally to represent the default action key
        return next(node, ""); // Empty string as internal key for default
    }

    // IBaseNode implementation for next (needed for polymorphism)
    std::shared_ptr<IBaseNode> next(std::shared_ptr<IBaseNode> node, const std::string& action) override {
         if (!node) {
            throw std::invalid_argument("Successor node cannot be null");
        }
        if (successors.count(action)) {
            logWarn("Overwriting successor for action '" + action + "' in node " + getClassName());
        }
        successors[action] = node;
        return node; // Return the base interface pointer
    }

    std::shared_ptr<IBaseNode> next(std::shared_ptr<IBaseNode> node) override {
        return next(node, ""); // Empty string for default
    }


    // --- Core Methods (to be implemented by subclasses) ---
    virtual P prep(Context& sharedContext) {
        // Default implementation returns default-constructed P
        // Handle void case explicitly if needed, though default works ok.
        if constexpr (std::is_same_v<P, void>) {
             return; // Or handle differently if void needs special logic
        } else {
            return P{};
        }
    }

    virtual E exec(P prepResult) = 0; // Pure virtual

    virtual std::optional<std::string> post(Context& sharedContext, const P& prepResult, const E& execResult) {
        // Default implementation returns nullopt (default action)
        return std::nullopt;
    }

    // --- Internal Execution Logic ---
protected:
    // This internal method allows Node<P,E> to override execution with retries
    virtual E internalExec(P prepResult) {
        return exec(std::move(prepResult)); // Use move if P is movable
    }

public:
    // IBaseNode implementation
    std::optional<std::string> internalRun(Context& sharedContext) override {
        P prepRes = prep(sharedContext);
        E execRes = internalExec(std::move(prepRes)); // Use move if P is movable
        // Need to handle void return type E potentially
        if constexpr (std::is_same_v<E, void>) {
             return post(sharedContext, prepRes, {}); // Pass dummy value for void E
        } else {
             return post(sharedContext, prepRes, execRes);
        }
    }


    // --- Standalone Run ---
    // Note: Return type matches internalRun now. The Java version returned String.
    // Returning optional<string> seems more consistent here.
    std::optional<std::string> run(Context& sharedContext) {
        if (!successors.empty()) {
            logWarn("Node " + getClassName() + " has successors, but run() was called. Successors won't be executed. Use Flow.");
        }
        return internalRun(sharedContext);
    }

    // --- Successor Retrieval (IBaseNode implementation) ---
    std::shared_ptr<IBaseNode> getNextNode(const std::optional<std::string>& action) const override {
        std::string actionKey = action.value_or(""); // Use "" for default action key
        auto it = successors.find(actionKey);
        if (it != successors.end()) {
            return it->second;
        } else {
            if (!successors.empty()) {
                std::string requestedAction = action.has_value() ? "'" + action.value() + "'" : "default";
                std::string availableActions;
                for(const auto& pair : successors) {
                    availableActions += "'" + (pair.first.empty() ? "<default>" : pair.first) + "' ";
                }
                 logWarn("Flow might end: Action " + requestedAction + " not found in successors ["
                         + availableActions + "] of node " + getClassName());
            }
            return nullptr; // No successor found
        }
    }

    bool hasSuccessors() const override {
        return !successors.empty();
    }

    const std::string& getClassName() const override {
        // Return a potentially mangled name. Provide a way to set a clean name if needed.
        // For now, use the stored approximation.
        // A better way: Add a virtual getName() method overridden by each concrete node.
        return className;
    }

protected:
     // Helper to safely get from map with default
     template<typename T>
     T getParamOrDefault(const std::string& key, T defaultValue) const {
         auto it = params.find(key);
         if (it != params.end()) {
             try {
                 return std::any_cast<T>(it->second);
             } catch (const std::bad_any_cast& e) {
                 // Log or handle cast error - return default for now
                 logWarn("Bad any_cast for param '" + key + "' in node " + getClassName() + ". Expected different type.");
                 return defaultValue;
             }
         }
         return defaultValue;
     }
};


// --- Synchronous Node with Retries ---
template <typename P, typename E>
class Node : public BaseNode<P, E> {
protected:
    int maxRetries;
    long long waitMillis; // Use long long for milliseconds
    int currentRetry = 0;

public:
    Node(int retries = 1, long long waitMilliseconds = 0)
        : maxRetries(retries), waitMillis(waitMilliseconds) {
        if (maxRetries < 1) throw std::invalid_argument("maxRetries must be at least 1");
        if (waitMillis < 0) throw std::invalid_argument("waitMillis cannot be negative");
    }

    virtual ~Node() override = default;

    // Fallback method to be overridden if needed
    virtual E execFallback(P prepResult, const std::exception& lastException) {
        // Default behavior is to re-throw the last exception
        throw PocketFlowException("Node execution failed after " + std::to_string(maxRetries) + " retries, and fallback was not implemented or also failed.", lastException);
    }

protected:
    // Override internalExec to add retry logic
    E internalExec(P prepResult) override {
        std::unique_ptr<std::exception> lastExceptionPtr; // Store last exception

        for (currentRetry = 0; currentRetry < maxRetries; ++currentRetry) {
            try {
                // Need to copy or move prepResult carefully if exec might modify it
                // Assuming exec takes by value or const ref for simplicity here
                // If P is expensive to copy, consider passing by ref and ensuring exec handles it.
                return this->exec(prepResult); // Call the user-defined exec
            } catch (const std::exception& e) {
                // Using unique_ptr to manage exception polymorphism if needed,
                // but storing a copy of the base std::exception might suffice.
                // Let's store the last exception message simply for now.
                // A better approach might involve exception_ptr.
                // lastException = e; // Direct copy loses polymorphic type

                // Store the exception *type* to rethrow properly or use std::exception_ptr
                 try { throw; } // Rethrow to capture current exception
                 catch (const std::exception& current_e) {
                     // Store the exception to be used in fallback
                      lastExceptionPtr = std::make_unique<std::runtime_error>(current_e.what()); // Store message
                 }


                if (currentRetry < maxRetries - 1 && waitMillis > 0) {
                    try {
                        std::this_thread::sleep_for(std::chrono::milliseconds(waitMillis));
                    } catch (...) {
                        // Handle potential exceptions during sleep? Unlikely but possible.
                        throw PocketFlowException("Thread interrupted during retry wait", std::runtime_error("sleep interrupted"));
                    }
                }
            } catch (...) { // Catch non-std exceptions if necessary
                 lastExceptionPtr = std::make_unique<std::runtime_error>("Non-standard exception caught during exec");
                 if (currentRetry < maxRetries - 1 && waitMillis > 0) {
                     std::this_thread::sleep_for(std::chrono::milliseconds(waitMillis));
                 }
            }
        }

        // If loop finishes, all retries failed
        try {
            if (!lastExceptionPtr) {
                 throw PocketFlowException("Execution failed after retries, but no exception was captured.");
            }
             // Call fallback, passing a reference to the stored exception approximation
            return execFallback(std::move(prepResult), *lastExceptionPtr);
        } catch (const std::exception& fallbackException) {
            // If fallback fails, throw appropriate exception
             throw PocketFlowException("Fallback execution failed after main exec retries failed.", fallbackException);
        } catch (...) {
             throw PocketFlowException("Fallback execution failed with non-standard exception.", std::runtime_error("Unknown fallback error"));
        }
    }
};


// --- Synchronous Batch Node ---
template <typename IN_ITEM, typename OUT_ITEM>
class BatchNode : public Node<std::vector<IN_ITEM>, std::vector<OUT_ITEM>> {
public:
    BatchNode(int retries = 1, long long waitMilliseconds = 0)
        : Node<std::vector<IN_ITEM>, std::vector<OUT_ITEM>>(retries, waitMilliseconds) {}

    virtual ~BatchNode() override = default;

    // --- Methods for subclasses to implement ---
    virtual OUT_ITEM execItem(const IN_ITEM& item) = 0; // Process a single item

    virtual OUT_ITEM execItemFallback(const IN_ITEM& item, const std::exception& lastException) {
        // Default fallback re-throws
         throw PocketFlowException("Batch item execution failed after retries, and fallback was not implemented or also failed.", lastException);
    }

    // --- Base class methods that MUST NOT be overridden by user ---
    // Make exec final to prevent accidental override. User should implement execItem.
    std::vector<OUT_ITEM> exec(std::vector<IN_ITEM> prepResult) final {
        // This method is conceptually hidden by internalExec override below.
        // We override internalExec directly.
        throw std::logic_error("BatchNode::exec should not be called directly.");
    }

     // Fallback for the whole batch (rarely needed if item fallback exists)
     std::vector<OUT_ITEM> execFallback(std::vector<IN_ITEM> prepResult, const std::exception& lastException) final {
         // This fallback applies if the *looping* itself fails, not individual items.
         throw PocketFlowException("BatchNode internal execution loop failed.", lastException);
     }


protected:
    // Override internalExec for batch processing logic
    std::vector<OUT_ITEM> internalExec(std::vector<IN_ITEM> batchPrepResult) override {
        if (batchPrepResult.empty()) {
            return {};
        }

        std::vector<OUT_ITEM> results;
        results.reserve(batchPrepResult.size());

        for (const auto& item : batchPrepResult) {
             std::unique_ptr<std::exception> lastItemExceptionPtr;
             bool itemSuccess = false;
             OUT_ITEM itemResult{}; // Default construct

             for (this->currentRetry = 0; this->currentRetry < this->maxRetries; ++this->currentRetry) {
                 try {
                     itemResult = execItem(item); // Call user implementation
                     itemSuccess = true;
                     break; // Success, exit retry loop for this item
                 } catch (const std::exception& e) {
                      try { throw; } catch(const std::exception& current_e) {
                          lastItemExceptionPtr = std::make_unique<std::runtime_error>(current_e.what());
                      }
                     if (this->currentRetry < this->maxRetries - 1 && this->waitMillis > 0) {
                         std::this_thread::sleep_for(std::chrono::milliseconds(this->waitMillis));
                     }
                 } catch (...) {
                      lastItemExceptionPtr = std::make_unique<std::runtime_error>("Non-standard exception during execItem");
                      if (this->currentRetry < this->maxRetries - 1 && this->waitMillis > 0) {
                         std::this_thread::sleep_for(std::chrono::milliseconds(this->waitMillis));
                      }
                 }
             } // End retry loop for item

             if (!itemSuccess) {
                 try {
                     if (!lastItemExceptionPtr) {
                         throw PocketFlowException("Item execution failed without exception for item."); // Add item info if possible
                     }
                     itemResult = execItemFallback(item, *lastItemExceptionPtr); // Call user fallback
                 } catch (const std::exception& fallbackEx) {
                      throw PocketFlowException("Item fallback execution failed.", fallbackEx); // Add item info if possible
                 } catch (...) {
                      throw PocketFlowException("Item fallback failed with non-standard exception.", std::runtime_error("Unknown item fallback error"));
                 }
             }
             results.push_back(std::move(itemResult)); // Move if possible
        } // End loop over items

        return results;
    }
};


// --- Flow Orchestrator ---
// Inherits from BaseNode with dummy types for consistency, but overrides run logic.
// Using std::nullptr_t for unused P type.
class Flow : public BaseNode<std::nullptr_t, std::optional<std::string>> {
protected:
    std::shared_ptr<IBaseNode> startNode = nullptr;

public:
    Flow() = default;
    explicit Flow(std::shared_ptr<IBaseNode> start) { this->start(std::move(start)); }
    virtual ~Flow() override = default;

    template <typename SN_P, typename SN_E>
    std::shared_ptr<BaseNode<SN_P, SN_E>> start(std::shared_ptr<BaseNode<SN_P, SN_E>> node) {
        if (!node) {
            throw std::invalid_argument("Start node cannot be null");
        }
        startNode = node; // Implicit cast to shared_ptr<IBaseNode>
        return node;
    }
     // Overload for IBaseNode pointer directly
     std::shared_ptr<IBaseNode> start(std::shared_ptr<IBaseNode> node) {
         if (!node) {
             throw std::invalid_argument("Start node cannot be null");
         }
         startNode = node;
         return node;
     }

    // Prevent direct calls to Flow's exec - logic is in orchestrate/internalRun
    std::optional<std::string> exec(std::nullptr_t /*prepResult*/) final override {
        throw std::logic_error("Flow::exec() is internal and should not be called directly. Use run().");
    }

protected:
    // The core orchestration logic
    virtual std::optional<std::string> orchestrate(Context& sharedContext, const Params& initialParams) {
        if (!startNode) {
            logWarn("Flow started with no start node.");
            return std::nullopt;
        }

        std::shared_ptr<IBaseNode> currentNode = startNode;
        std::optional<std::string> lastAction = std::nullopt;

        // Combine flow's base params with initial params for this run
        Params currentRunParams = this->params; // Start with Flow's own params
        currentRunParams.insert(initialParams.begin(), initialParams.end()); // Add/overwrite with initialParams


        while (currentNode != nullptr) {
            currentNode->setParamsInternal(currentRunParams); // Set params for the current node
            lastAction = currentNode->internalRun(sharedContext); // Execute the node
            currentNode = currentNode->getNextNode(lastAction);   // Find the next node based on action
        }

        return lastAction; // Return the action that led to termination (or nullopt if last node had no action)
    }

    // Override BaseNode's internal run
     std::optional<std::string> internalRun(Context& sharedContext) override {
        // Flow's prep is usually no-op unless overridden
        [[maybe_unused]] std::nullptr_t prepRes = prep(sharedContext); // Call prep, ignore result

        // Orchestrate starting with empty initial params (can be overridden by BatchFlow)
        std::optional<std::string> orchRes = orchestrate(sharedContext, {});

        // Flow's post processes the *result* of the orchestration
        return post(sharedContext, nullptr, orchRes);
    }

public:
    // Override post for Flow. Default returns the final action from orchestration.
    std::optional<std::string> post(Context& sharedContext, const std::nullptr_t& /*prepResult*/, const std::optional<std::string>& execResult) override {
        return execResult; // Simply pass through the last action
    }

    // run() method inherited from BaseNode calls internalRun() correctly.
};


// --- Batch Flow ---
class BatchFlow : public Flow {
public:
    BatchFlow() = default;
    explicit BatchFlow(std::shared_ptr<IBaseNode> start) : Flow(std::move(start)) {}
    virtual ~BatchFlow() override = default;

    // --- Methods for subclasses to implement ---
    virtual std::vector<Params> prepBatch(Context& sharedContext) = 0;

    // Post method after all batches have run
    virtual std::optional<std::string> postBatch(Context& sharedContext, const std::vector<Params>& batchPrepResult) = 0;

protected:
    // Override internalRun to handle batch execution
    std::optional<std::string> internalRun(Context& sharedContext) override {
        std::vector<Params> batchParamsList = prepBatch(sharedContext);

        if (batchParamsList.empty()) {
             logWarn("BatchFlow prepBatch returned empty list.");
             // Still call postBatch even if empty
        }

        for (const auto& batchParams : batchParamsList) {
            // Run the orchestration for each parameter set.
            // Result of individual orchestrations is ignored here; focus is on side effects.
            orchestrate(sharedContext, batchParams);
        }

        // After all batches, call postBatch
        return postBatch(sharedContext, batchParamsList);
    }

public:
    // Prevent calling the regular post method directly for BatchFlow
    std::optional<std::string> post(Context& /*sharedContext*/, const std::nullptr_t& /*prepResult*/, const std::optional<std::string>& /*execResult*/) final override {
         throw std::logic_error("Use postBatch for BatchFlow, not post.");
    }
};


} // namespace pocketflow

#endif // POCKETFLOW_H