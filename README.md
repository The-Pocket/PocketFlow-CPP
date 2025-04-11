# PocketFlow C++

A minimalist LLM framework, ported from Python to C++.

## Overview

PocketFlow C++ is a port of the original [Python PocketFlow](https://github.com/The-Pocket/PocketFlow). It provides a lightweight, flexible system for building and executing workflows through a simple node-based architecture using modern C++.

> **Note:** This is an initial implementation ported from the Python version. It currently only supports synchronous operations. Community contributions are welcome to enhance and maintain this project, particularly in adding asynchronous capabilities and comprehensive testing.

## Features

*   **Node-Based Architecture:** Define workflows by connecting distinct processing units (nodes).
*   **Type-Safe (within C++ limits):** Uses C++ templates for node input/output types. `std::any` is used for flexible context and parameters.
*   **Synchronous Execution:** Simple, predictable execution flow (async is a future goal).
*   **Context Propagation:** Share data between nodes using a `Context` map (`std::map<std::string, std::any>`).
*   **Configurable Nodes:** Pass parameters to nodes using a `Params` map (also `std::map<std::string, std::any>`).
*   **Retry Logic:** Built-in optional retry mechanism for `Node` operations.
*   **Batch Processing:** Includes `BatchNode` and `BatchFlow` for processing lists of items or parameter sets.
*   **Header-Only:** The core library is provided in `PocketFlow.h` for easy integration.

## Requirements

*   **C++17 Compliant Compiler:** Required for `std::any` and `std::optional`. (e.g., GCC 7+, Clang 5+, MSVC 19.14+)
*   **CMake:** Version 3.10 or higher (for C++17 support).

## Building

The library itself is header-only (`PocketFlow.h`). To build the example provided (`main.cpp`):

1.  Ensure you have CMake and a C++17 compiler installed.
2.  Create a build directory:
    ```bash
    mkdir build
    cd build
    ```
3.  Run CMake to configure the project:
    ```bash
    cmake ..
    ```
4.  Build the executable:
    ```bash
    cmake --build .
    # Or use make, ninja, etc. depending on your generator
    # make
    ```
5.  The example executable (e.g., `pocketflow_example`) will be inside the `build` directory.
    ```bash
    ./pocketflow_example
    ```

## Usage

Here's a simple example demonstrating how to define and run a workflow:

```cpp
#include "pocketflow.h" // Include the library header
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::make_shared

// Use the namespace
using namespace pocketflow;

// --- Define Custom Nodes ---

// Start Node: Takes no input (nullptr_t), returns a string action
class MyStartNode : public Node<std::nullptr_t, std::string> {
public:
    std::string exec(std::nullptr_t /*prepResult*/) override {
        std::cout << "Starting workflow..." << std::endl;
        return "started"; // This string determines the next node
    }
    // Optional: override post to return the action explicitly
    std::optional<std::string> post(Context& ctx, const std::nullptr_t& p, const std::string& e) override {
         return e; // Return exec result as the action
     }
};

// End Node: Takes a string from prep, returns nothing (nullptr_t)
class MyEndNode : public Node<std::string, std::nullptr_t> {
public:
    // Optional: Prepare input for exec, potentially using context
    std::string prep(Context& ctx) override {
        return "Preparing to end workflow";
    }

    // Execute the node's main logic
    std::nullptr_t exec(std::string prepResult) override {
        std::cout << "Ending workflow with: " << prepResult << std::endl;
        return nullptr; // Return value for void exec
    }
     // Optional: post can modify context, default returns no action (ends flow here)
};


int main() {
    // --- Create Node Instances (use std::shared_ptr) ---
    auto startNode = std::make_shared<MyStartNode>();
    auto endNode = std::make_shared<MyEndNode>();

    // --- Connect Nodes ---
    // When startNode returns the action "started", execute endNode next.
    startNode->next(endNode, "started");

    // --- Create and Configure Flow ---
    Flow flow(startNode); // Initialize the flow with the starting node

    // --- Prepare Context and Run ---
    Context sharedContext; // Map to share data between nodes
    std::cout << "Executing workflow..." << std::endl;
    flow.run(sharedContext); // Execute the workflow
    std::cout << "Workflow completed successfully." << std::endl;

    return 0;
}
```

## Core Concepts

*   **`IBaseNode` / `BaseNode<P, E>`:** The fundamental building block.
    *   `P`: The type returned by the `prep` method (input to `exec`). Use `std::nullptr_t` for no input.
    *   `E`: The type returned by the `exec` method (input to `post`). Use `std::nullptr_t` for no return value.
    *   `prep(Context&)`: Prepare data needed for `exec`. Can use/modify the shared context.
    *   `exec(P)`: Execute the core logic of the node.
    *   `post(Context&, P, E)`: Process results after `exec`. Can use/modify context. Returns `std::optional<std::string>` which is the *action* determining the next node. `std::nullopt` triggers the default transition.
    *   `next(node, action)`: Connects this node to the `node` when the `action` string is returned by `post`. `next(node)` connects via the default action.
*   **`Node<P, E>`:** A `BaseNode` with added retry logic (`maxRetries`, `waitMillis`, `execFallback`).
*   **`BatchNode<IN, OUT>`:** A `Node` that processes a `std::vector<IN>` and produces a `std::vector<OUT>`, handling retries per item via `execItem` and `execItemFallback`.
*   **`Flow`:** Orchestrates the execution of connected nodes starting from a designated `startNode`.
*   **`BatchFlow`:** A `Flow` that runs its entire sequence for multiple parameter sets generated by `prepBatch`.
*   **`Context` (`std::map<std::string, std::any>`):** A shared data store passed through the workflow, allowing nodes to communicate indirectly. Requires careful type casting (`std::any_cast`).
*   **`Params` (`std::map<std::string, std::any>`):** Configuration parameters passed to a node instance, typically set before execution or by a `BatchFlow`.

## C++ Specifics (vs. Java/Python)

*   **Memory Management:** Uses `std::shared_ptr` for managing node object lifetimes within the workflow graph.
*   **Type Erasure:** `std::any` is used for `Context` and `Params`, requiring explicit `std::any_cast` and careful handling of potential `std::bad_any_cast` exceptions.
*   **`void` Types:** `std::nullptr_t` is used as a placeholder template argument for `P` or `E` when a node doesn't logically take input (`prep` returns nothing) or produce output (`exec` returns nothing).
*   **Actions:** Node transitions are determined by `std::optional<std::string>` returned from `post`. `std::nullopt` signifies the default transition (if one is defined using `next(node)`).
*   **Header-Only:** Simplifies integration – just include `PocketFlow.h`.

## Development

### Building the Example/Tests

Use the CMake instructions provided in the "Building" section.

### Running Tests

(Currently, no automated test suite like JUnit is included. This is a key area for contribution!)
You would typically integrate a testing framework like GoogleTest:
1.  Set up GoogleTest in your project.
2.  Write test cases in a separate `.cpp` file (e.g., `PocketFlow_test.cpp`).
3.  Configure CMake (as shown commented out in the example `CMakeLists.txt`) to build and run the tests.

```bash
# Example CMake commands after GoogleTest setup
cd build
cmake ..
cmake --build .
ctest # Run tests
```

## Contributing

Contributions are highly welcome! We are particularly looking for help with:

1.  **Asynchronous Operations:** Implementing non-blocking node execution (e.g., using `std::async`, `std::thread`, futures, or an external async library).
2.  **Testing:** Adding comprehensive unit and integration tests using a framework like GoogleTest.
3.  **Documentation:** Improving explanations, adding more examples, and documenting edge cases.
4.  **Error Handling:** Refining exception types and context propagation for errors.
5.  **Examples:** Providing more practical examples, potentially related to LLM interactions.

Please feel free to submit pull requests or open issues for discussion.

## License

[MIT License](LICENSE)