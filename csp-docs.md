# Using the `csp` C++ Engine

This file provides a basic guide on how to use the CSP engine.

## Core Concepts

The CSP engine is built around a few core concepts:

-   **TimeSeries:** Represents a stream of values over time. Each value has a timestamp and a corresponding data payload.
-   **Node:** A processing unit that takes one or more `TimeSeries` as input and produces one or more `TimeSeries` as output.
-   **Graph:** A collection of interconnected nodes that define the data flow.
-   **Engine:** The runtime environment that executes the graph, managing time and data propagation.

## Building a Graph

To build a `csp` graph in C++, you'll need to include the relevant headers. For this example, we'll need:

```cpp
#include <csp/engine/CppNode.h>
#include <csp/engine/RootEngine.h>
#include <iostream>

using namespace csp;
```

### Defining Nodes

Here’s how you can define a few different types of nodes: a source, a processor, and a sink.

#### Source Node: `NumberSource`

This node generates a sequence of `double` values from a given start value and count.

```cpp
DECLARE_CPPNODE(NumberSource)
{
    SCALAR_INPUT(double, start);
    SCALAR_INPUT(int, count);
    TS_NAMED_OUTPUT(double, value);

    STATE_VAR(int, m_counter);

    INIT_CPPNODE(NumberSource)
    {
    }

    START()
    {
        m_counter = 0;
        schedule_alarm(value, now(), 0.0);
    }

    INVOKE()
    {
        if (m_counter < count)
        {
            value.output(start + m_counter);
            m_counter++;
            schedule_alarm(value, now() + TimeDelta::fromSeconds(1), 0.0);
        }
    }
};
```

#### Processing Node: `AddNode`

This node takes two `TimeSeries` of `double` and outputs their sum.

```cpp
DECLARE_CPPNODE(AddNode)
{
    TS_INPUT(double, x);
    TS_INPUT(double, y);
    TS_NAMED_OUTPUT(double, sum);

    INIT_CPPNODE(AddNode)
    {
    }

    INVOKE()
    {
        if (ticked(x, y))
        {
            sum.output(x.lastValue() + y.lastValue());
        }
    }
};
```

#### Sink Node: `PrintNode`

This node prints the values from its input `TimeSeries` to the console.

```cpp
DECLARE_CPPNODE(PrintNode)
{
    TS_INPUT(double, input);

    INIT_CPPNODE(PrintNode)
    {
    }

    INVOKE()
    {
        if (ticked(input))
        {
            std::cout << now() << " PrintNode: " << input.lastValue() << std::endl;
        }
    }
};
```

## Running the Graph

With the nodes defined, you can now construct and run the graph in your `main` function.

```cpp
int main()
{
    // Create the engine
    RootEngine engine;

    // Define the graph wiring
    engine.createCppNode(
        "source1", NumberSource::create,
        {{"start", 10.0}, {"count", 5}}
    );

    engine.createCppNode(
        "source2", NumberSource::create,
        {{"start", 20.0}, {"count", 5}}
    );

    engine.createCppNode(
        "add", AddNode::create,
        {{"x", engine.getTs("source1", "value")},
         {"y", engine.getTs("source2", "value")}}
    );

    engine.createCppNode(
        "print", PrintNode::create,
        {{"input", engine.getTs("add", "sum")}}
    );

    // Run the engine
    engine.run(now(), now() + TimeDelta::fromSeconds(10));

    return 0;
}
```
