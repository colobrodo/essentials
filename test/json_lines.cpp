#include <iostream>

#include "../include/essentials.hpp"

using namespace essentials;

int main() {
    json_lines jl;

    jl.add("sum", "13");
    double avg = 325.7;
    jl.add("average", avg);

    auto stats = jl.add_object("stats");
    stats.add("minimum", 12);
    stats.add("maximum", 8894);

    auto samples = stats.add_array("samples");
    samples.add(42);
    samples.add(325.7);

    auto sample = samples.add_object();
    sample.add("label", "representative");

    auto groups = samples.add_array();
    groups.add("first");

    // print current line
    jl.print_line();

    jl.new_line();
    jl.add("foo", avg);
    jl.add("bar", 8894);
    jl.add("baz", "value");

    // Handles remain attached to their original line.
    stats.add("count", 3);

    std::string str("hi there");
    jl.add("bit", str.c_str());

    // print all lines
    jl.print();

    return 0;
}
