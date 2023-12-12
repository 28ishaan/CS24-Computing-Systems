#include "jvm.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "heap.h"
#include "read_class.h"

/** The name of the method to invoke to run the class file */
const char MAIN_METHOD[] = "main";
/**
 * The "descriptor" string for main(). The descriptor encodes main()'s signature,
 * i.e. main() takes a String[] and returns void.
 * If you're interested, the descriptor string is explained at
 * https://docs.oracle.com/javase/specs/jvms/se12/html/jvms-4.html#jvms-4.3.2.
 */
const char MAIN_DESCRIPTOR[] = "([Ljava/lang/String;)V";

/**
 * Represents the return value of a Java method: either void or an int or a reference.
 * For simplification, we represent a reference as an index into a heap-allocated array.
 * (In a real JVM, methods could also return object references or other primitives.)
 */
typedef struct {
    /** Whether this returned value is an int */
    bool has_value;
    /** The returned value (only valid if `has_value` is true) */
    int32_t value;
} optional_value_t;

int stack_help(int32_t *stack, size_t stack_size, int32_t val, size_t num) {
    stack[stack_size - num] = val;
    return stack_size - num + 1;
}

/**
 * Runs a method's instructions until the method returns.
 *
 * @param method the method to run
 * @param locals the array of local variables, including the method parameters.
 *   Except for parameters, the locals are uninitialized.
 * @param class the class file the method belongs to
 * @param heap an array of heap-allocated pointers, useful for references
 * @return an optional int containing the method's return value
 */
optional_value_t execute(method_t *method, int32_t *locals, class_file_t *class,
                         heap_t *heap) {
    size_t program_counter = 0;
    int32_t *operand_stack = calloc(sizeof(int32_t), (method->code.max_stack));
    int32_t stack_size = 0;
    while (program_counter < method->code.code_length) {
        switch (method->code.code[program_counter]) {
            case i_bipush: {
                stack_size =
                    stack_help(operand_stack, stack_size,
                               (int8_t) method->code.code[program_counter + 1], 0);
                program_counter += 2;
                break;
            }
            case i_iadd: {
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 1] + operand_stack[stack_size - 2], 2);
                program_counter++;
                break;
            }
            case i_return: {
                optional_value_t result = {.has_value = false};
                free(operand_stack);
                return result;
            }
            case i_getstatic: {
                program_counter += 3;
                break;
            }
            case i_invokevirtual: {
                printf("%i\n", operand_stack[stack_size - 1]);
                stack_size--;
                program_counter += 3;
                break;
            }
            case i_iconst_m1:
            case i_iconst_0:
            case i_iconst_1:
            case i_iconst_2:
            case i_iconst_3:
            case i_iconst_4:
            case i_iconst_5: {
                operand_stack[stack_size] =
                    method->code.code[program_counter] - i_iconst_m1 - 1;
                stack_size++;
                program_counter++;
                break;
            }
            case i_sipush: {
                signed short val = (method->code.code[program_counter + 1] << 8) |
                                   method->code.code[program_counter + 2];
                operand_stack[stack_size] = val;
                stack_size++;
                program_counter += 3;
                break;
            }
            case i_isub: {
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] - operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_imul: {
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] * operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_idiv: {
                assert(operand_stack[stack_size - 1] != 0);
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] / operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_irem: {
                assert(operand_stack[stack_size - 1] != 0);
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] % operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_ineg: {
                int32_t neg = operand_stack[stack_size - 1] * -1;
                operand_stack[stack_size - 1] = neg;
                program_counter++;
                break;
            }
            case i_ishl: {
                assert(operand_stack[stack_size - 1] >= 0);
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] << operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_ishr: {
                assert(operand_stack[stack_size - 1] >= 0);
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] >> operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_iushr: {
                assert(operand_stack[stack_size - 1] >= 0);
                stack_size = stack_help(operand_stack, stack_size,
                                        (unsigned) operand_stack[stack_size - 2] >>
                                            operand_stack[stack_size - 1],
                                        2);
                program_counter++;
                break;
            }
            case i_iand: {
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] & operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_ior: {
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] | operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_ixor: {
                stack_size = stack_help(
                    operand_stack, stack_size,
                    operand_stack[stack_size - 2] ^ operand_stack[stack_size - 1], 2);
                program_counter++;
                break;
            }
            case i_iload: {
                operand_stack[stack_size] =
                    locals[method->code.code[program_counter + 1]];
                stack_size++;
                program_counter += 2;
                break;
            }
            case i_istore: {
                uint32_t val = operand_stack[stack_size - 1];
                locals[method->code.code[program_counter + 1]] = val;
                stack_size--;
                program_counter += 2;
                break;
            }
            case i_iload_0:
            case i_iload_1:
            case i_iload_2:
            case i_iload_3:
                operand_stack[stack_size] =
                    locals[((int32_t) method->code.code[program_counter]) - i_iload_0];
                stack_size++;
                program_counter++;
                break;
            case i_istore_0:
            case i_istore_1:
            case i_istore_2:
            case i_istore_3:
                locals[((int32_t) method->code.code[program_counter]) - i_istore_0] =
                    operand_stack[stack_size - 1];
                stack_size--;
                program_counter++;
                break;
            case i_iinc: {
                int8_t b = method->code.code[program_counter + 2];
                locals[method->code.code[program_counter + 1]] += b;
                program_counter += 3;
                break;
            }
            case i_ldc: {
                int32_t b = (int32_t) method->code.code[program_counter + 1];
                operand_stack[stack_size] =
                    ((CONSTANT_Integer_info *) class->constant_pool[b - 1].info)->bytes;
                stack_size++;
                program_counter += 2;
                break;
            }
            case i_ifeq: {
                int32_t a = operand_stack[stack_size - 1];
                if (a == 0) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size--;
                break;
            }
            case i_ifne: {
                int32_t a = operand_stack[stack_size - 1];
                if (a != 0) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size--;
                break;
            }
            case i_iflt: {
                int32_t a = operand_stack[stack_size - 1];
                if (a < 0) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size--;
                break;
            }
            case i_ifge: {
                int32_t a = operand_stack[stack_size - 1];
                if (a >= 0) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size--;
                break;
            }
            case i_ifgt: {
                int32_t a = operand_stack[stack_size - 1];
                if (a > 0) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size--;
                break;
            }
            case i_ifle: {
                int32_t a = operand_stack[stack_size - 1];
                if (a <= 0) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size--;
                break;
            }
            case i_if_icmpeq: {
                int32_t b = operand_stack[stack_size - 1];
                int32_t a = operand_stack[stack_size - 2];
                if (a == b) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size -= 2;
                break;
            }
            case i_if_icmpne: {
                int32_t b = operand_stack[stack_size - 1];
                int32_t a = operand_stack[stack_size - 2];
                if (a != b) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size -= 2;
                break;
            }
            case i_if_icmplt: {
                int32_t b = operand_stack[stack_size - 1];
                int32_t a = operand_stack[stack_size - 2];
                if (a < b) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size -= 2;
                break;
            }
            case i_if_icmpge: {
                int32_t b = operand_stack[stack_size - 1];
                int32_t a = operand_stack[stack_size - 2];
                if (a >= b) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size -= 2;
                break;
            }
            case i_if_icmpgt: {
                int32_t b = operand_stack[stack_size - 1];
                int32_t a = operand_stack[stack_size - 2];
                if (a > b) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size -= 2;
                break;
            }
            case i_if_icmple: {
                int32_t b = operand_stack[stack_size - 1];
                int32_t a = operand_stack[stack_size - 2];
                if (a <= b) {
                    int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                    int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                    program_counter += (int16_t)((b1 << 8) | b2);
                }
                else {
                    program_counter += 3;
                }
                stack_size -= 2;
                break;
            }
            case i_goto: {
                int32_t b1 = (int32_t) method->code.code[program_counter + 1];
                int32_t b2 = (int32_t) method->code.code[program_counter + 2];
                program_counter += (int16_t)((b1 << 8) | b2);
                break;
            }
            case i_ireturn: {
                optional_value_t a;
                a.value = operand_stack[stack_size - 1];
                a.has_value = true;
                free(operand_stack);
                return a;
            }
            case i_invokestatic: {
                uint8_t b1 = method->code.code[program_counter + 1];
                uint8_t b2 = method->code.code[program_counter + 2];
                int16_t index = (int16_t)((b1 << 8) | b2);
                method_t *meth = find_method_from_index(index, class);
                uint16_t len = get_number_of_parameters(meth);
                int32_t *locals_queue = calloc(sizeof(int32_t), (meth->code.max_locals));
                for (int32_t i = len - 1; i >= 0; i--) {
                    locals_queue[i] = operand_stack[stack_size - 1];
                    stack_size -= 1;
                }
                optional_value_t res = execute(meth, locals_queue, class, heap);
                free(locals_queue);
                if (res.has_value) {
                    operand_stack[stack_size] = res.value;
                    stack_size += 1;
                }
                program_counter += 3;
                break;
            }
            case i_nop: {
                program_counter++;
                break;
            }
            case i_dup: {
                int32_t a = operand_stack[stack_size - 1];
                operand_stack[stack_size] = a;
                stack_size++;
                program_counter++;
                break;
            }
            case i_newarray: {
                int32_t count = operand_stack[stack_size - 1];
                assert(count + 1 > 0);
                int32_t *newarr = calloc(sizeof(int32_t), count + 1);
                newarr[0] = count;
                for (int i = 1; i < count + 1; i++) {
                    newarr[i] = 0;
                }
                int32_t ref = heap_add(heap, newarr);
                operand_stack[stack_size - 1] = ref;
                program_counter += 2;
                break;
            }
            case i_arraylength: {
                int32_t ref = operand_stack[stack_size - 1];
                int32_t len = heap_get(heap, ref)[0];
                operand_stack[stack_size - 1] = len;
                program_counter++;
                break;
            }
            case i_areturn: {
                int32_t ref = operand_stack[stack_size - 1];
                optional_value_t res;
                res.value = ref;
                res.has_value = true;
                free(operand_stack);
                return res;
            }
            case i_iastore: {
                int32_t value = operand_stack[stack_size - 1];
                int32_t index = operand_stack[stack_size - 2];
                int32_t ref = operand_stack[stack_size - 3];
                int32_t *arr = heap_get(heap, ref);
                arr[index + 1] = value;
                stack_size -= 3;
                program_counter++;
                break;
            }
            case i_iaload: {
                int32_t index = operand_stack[stack_size - 1];
                int32_t ref = operand_stack[stack_size - 2];
                int32_t val = heap_get(heap, ref)[index + 1];
                stack_size -= 2;
                operand_stack[stack_size] = val;
                stack_size++;
                program_counter++;
                break;
            }
            case i_aload: {
                operand_stack[stack_size] =
                    locals[method->code.code[program_counter + 1]];
                stack_size++;
                program_counter += 2;
                break;
            }
            case i_astore: {
                uint32_t val = operand_stack[stack_size - 1];
                locals[method->code.code[program_counter + 1]] = val;
                stack_size--;
                program_counter += 2;
                break;
            }
            case i_aload_0:
            case i_aload_1:
            case i_aload_2:
            case i_aload_3:
                operand_stack[stack_size] =
                    locals[((int32_t) method->code.code[program_counter]) - i_aload_0];
                stack_size++;
                program_counter++;
                break;
            case i_astore_0:
            case i_astore_1:
            case i_astore_2:
            case i_astore_3:
                locals[((int32_t) method->code.code[program_counter]) - i_astore_0] =
                    operand_stack[stack_size - 1];
                stack_size--;
                program_counter++;
                break;
        }
    }

    // Return void
    optional_value_t result = {.has_value = false};
    free(operand_stack);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <class file>\n", argv[0]);
        return 1;
    }

    // Open the class file for reading
    FILE *class_file = fopen(argv[1], "r");
    assert(class_file != NULL && "Failed to open file");

    // Parse the class file
    class_file_t *class = get_class(class_file);
    int error = fclose(class_file);
    assert(error == 0 && "Failed to close file");

    // The heap array is initially allocated to hold zero elements.
    heap_t *heap = heap_init();

    // Execute the main method
    method_t *main_method = find_method(MAIN_METHOD, MAIN_DESCRIPTOR, class);
    assert(main_method != NULL && "Missing main() method");
    /* In a real JVM, locals[0] would contain a reference to String[] args.
     * But since TeenyJVM doesn't support Objects, we leave it uninitialized. */
    int32_t locals[main_method->code.max_locals];
    // Initialize all local variables to 0
    memset(locals, 0, sizeof(locals));
    optional_value_t result = execute(main_method, locals, class, heap);
    assert(!result.has_value && "main() should return void");

    // Free the internal data structures
    free_class(class);

    // Free the heap
    heap_free(heap);
}
