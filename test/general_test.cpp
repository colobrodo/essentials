#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <type_traits>

#include "../include/essentials.hpp"

// ==========================================
// MOCK STRUCTURES FOR TESTING
// ==========================================

struct NonPodStruct {
    std::vector<int> data;
    virtual ~NonPodStruct() = default;  // Makes it strictly non-POD/non-trivial
};

struct PodStruct {
    uint32_t a;
    double b;
};

// ==========================================
// TEST MACROS
// ==========================================

#define RUN_TEST(TestFunc)                                      \
    std::cout << "[TEST] " << #TestFunc << "..." << std::flush; \
    TestFunc();                                                 \
    std::cout << " PASS\n";

#define ASSERT_THROWS(Expr, ExceptionType)                            \
    do {                                                              \
        bool threw = false;                                           \
        try {                                                         \
            Expr;                                                     \
        } catch (ExceptionType const&) {                              \
            threw = true;                                             \
        } catch (...) {                                               \
        }                                                             \
        assert(threw && "Expected exception was not thrown: " #Expr); \
        (void)threw;                                                  \
    } while (0)

// ==========================================
// THE PEDANTIC TESTS
// ==========================================

void test_type_traits() {
    // Check POD trait
    assert(essentials::is_pod<int>::value == true);
    assert(essentials::is_pod<PodStruct>::value == true);
    assert(essentials::is_pod<NonPodStruct>::value == false);
    assert(essentials::is_pod<std::vector<int>>::value == false);

    // Check owning_span trait
    assert(essentials::is_owning_span_v<essentials::owning_span<int>> == true);
    assert(essentials::is_owning_span_v<std::vector<int>> == false);
    assert(essentials::is_owning_span_v<int> == false);
}

void test_advanced_timer() {
    essentials::timer_type t;

    uint64_t ignore = 0;
    for (int i = 0; i < 6; ++i) {
        t.start();
        for (int j = 0; j < 10000000; ++j) ignore ^= j;
        t.stop();
    }
    essentials::do_not_optimize_away(ignore);

    assert(t.runs() == 6);

    // Discard the slowest, fastest, and the first (warmup)
    t.discard_first();
    assert(t.runs() == 5);

    t.discard_max();
    assert(t.runs() == 4);

    t.discard_min();
    assert(t.runs() == 3);

    assert(t.average() > 0.0);

    t.reset();
    assert(t.runs() == 0);
    assert(t.elapsed() == 0.0);
}

void test_owning_span_models() {
    // Model 1: Heap-Owned (Rvalue / Moved)
    {
        std::vector<int> src = {1, 2, 3};
        essentials::owning_span<int> span(std::move(src));
        assert(span.size() == 3);
        assert(span.front() == 1 && span.back() == 3);
        assert(!span.empty());
    }

    // Model 2: Externally-Owned (e.g., mock mmap)
    {
        int raw_data[] = {10, 20, 30, 40};
        bool deleter_called = false;

        {
            std::shared_ptr<void> fake_owner(&raw_data, [&](void*) { deleter_called = true; });
            essentials::owning_span<int> span(raw_data, 4, fake_owner);
            assert(span.size() == 4);
            assert(span[2] == 30);
        }
        // Span and owner went out of scope, deleter should have fired
        assert(deleter_called);
    }

    // Model 3: Unowned (Raw Pointer)
    {
        int raw_data[] = {99, 100};
        essentials::owning_span<int> span(raw_data, 2);
        assert(span.size() == 2);
        assert(span[0] == 99);

        // Test clear and swap
        essentials::owning_span<int> empty_span;
        assert(empty_span.empty());

        span.swap(empty_span);
        assert(span.empty());
        assert(empty_span.size() == 2);

        empty_span.clear();
        assert(empty_span.empty());
        assert(empty_span.data() == nullptr);
    }
}

void test_serialization_edge_cases() {
    const char* file = "test_empty.bin";

    // Test serializing empty containers
    std::vector<int> empty_vec;
    essentials::owning_span<int> empty_span;

    {
        essentials::saver s(file);
        s.visit(empty_vec);
        s.visit(empty_span);
    }

    {
        std::vector<int> loaded_vec;
        essentials::owning_span<int> loaded_span;

        essentials::loader l(file);
        l.visit(loaded_vec);
        l.visit(loaded_span);

        assert(loaded_vec.empty());
        assert(loaded_span.empty());
    }

    std::remove(file);
}

void test_allocator_exceptions() {
    // Test the contiguous_memory_allocator boundary checks
    // We will spoof a visitor by passing a small dummy buffer
    uint8_t dummy_buffer[10];
    const char* dummy_file = "dummy_file.bin";  // Just needs to exist for ifstream

    // Create dummy file
    FILE* f = fopen(dummy_file, "w");
    if (f) {
        fputs("0", f);
        fclose(f);
    }

    essentials::contiguous_memory_allocator::visitor v(dummy_buffer, 10, dummy_file);

    // Consume 4 bytes -> OK (Total: 4/10)
    v.consume(4);
    assert(v.allocated() == 4);

    // Consume 6 bytes -> OK (Total: 10/10)
    v.consume(6);
    assert(v.allocated() == 10);

    // Consume 1 more byte -> MUST THROW
    ASSERT_THROWS(v.consume(1), std::runtime_error);

    std::remove(dummy_file);
}

void test_json_lines_edge_cases() {
    auto read_file = [](char const* filename) {
        std::ifstream input(filename, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };

    const char* file = "test_json_nested.jsonl";
    {
        essentials::json_lines j;
        j.add("empty_string", "");
        j.add("zero_val", 0);
        j.add("bool_false", false);

        auto object = j.add_object("my_object");
        object.add("key", "value");
        auto nested = object.add_object("nested");
        nested.add("enabled", true);

        auto array = object.add_array("items");
        array.add("42");
        auto array_object = array.add_object();
        array_object.add("answer", 42);
        auto nested_array = array.add_array();
        nested_array.add(7);

        object.add_object("empty_object");
        object.add_array("empty_array");
        j.save_to_file(file);
    }
    assert(read_file(file) ==
           "{\"empty_string\": \"\", \"zero_val\": \"0\", "
           "\"bool_false\": \"false\", \"my_object\": {\"key\": \"value\", "
           "\"nested\": {\"enabled\": \"true\"}, \"items\": [\"42\", "
           "{\"answer\": \"42\"}, [\"7\"]], \"empty_object\": {}, "
           "\"empty_array\": []}}\n");
    std::remove(file);

    const char* escaped_file = "test_json_escaped.jsonl";
    {
        essentials::json_lines j;
        std::string value = "quote\" newline\n tab\t slash\\ control";
        value.push_back('\x01');
        j.add("key\"\\\n", value);
        j.save_to_file(escaped_file);
    }
    assert(read_file(escaped_file) ==
           "{\"key\\\"\\\\\\n\": \"quote\\\" newline\\n tab\\t slash\\\\ "
           "control\\u0001\"}\n");
    std::remove(escaped_file);

    const char* stable_file = "test_json_stable_handles.jsonl";
    {
        essentials::json_lines j;
        auto retained = j.add_object("retained");
        for (int i = 0; i != 256; ++i) j.add("root", i);
        j.new_line();
        j.add("second", "line");
        retained.add("late", "addition");
        j.save_to_file(stable_file);
    }
    auto stable_output = read_file(stable_file);
    assert(stable_output.find("\"retained\": {\"late\": \"addition\"}") !=
           std::string::npos);
    assert(stable_output.find("\n{\"second\": \"line\"}\n") !=
           std::string::npos);
    std::remove(stable_file);

    const char* original_file = "test_json_original.jsonl";
    const char* copy_file = "test_json_copy.jsonl";
    {
        essentials::json_lines original;
        auto child = original.add_object("child");
        child.add("shared_at_copy", 1);

        essentials::json_lines copy = original;
        child.add("original_only", 2);
        copy.add("copy_only", 3);

        original.save_to_file(original_file);
        copy.save_to_file(copy_file);
    }
    assert(read_file(original_file) ==
           "{\"child\": {\"shared_at_copy\": \"1\", "
           "\"original_only\": \"2\"}}\n");
    assert(read_file(copy_file) ==
           "{\"child\": {\"shared_at_copy\": \"1\"}, "
           "\"copy_only\": \"3\"}\n");
    std::remove(original_file);
    std::remove(copy_file);
}

int main() {
    std::cout << "Running a simple test suite...\n";
    std::cout << "-------------------------------------------\n";

    RUN_TEST(test_type_traits);
    RUN_TEST(test_advanced_timer);
    RUN_TEST(test_owning_span_models);
    RUN_TEST(test_serialization_edge_cases);
    RUN_TEST(test_allocator_exceptions);
    RUN_TEST(test_json_lines_edge_cases);

    std::cout << "-------------------------------------------\n";
    std::cout << "All tests passed!\n";

    return 0;
}
