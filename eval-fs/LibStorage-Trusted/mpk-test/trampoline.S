/* 
   parameters:
   rdi = function pointer
   rsi = function arg
   rdx = new stack pointer 

   new stack layout（from high address to low address）：
   
    +-----------------+ <-- new_stack_ptr
    | Original stack pointer (rsp) | new_stack_ptr -8
    | Target function pointer | new_stack_ptr -16
    | Function Parameter | new_stack_ptr -24
    | Function Return Value | new_stack_ptr -32
    +---------------------actual rsp after switching.
*/

.text
.global trampoline
.type trampoline, @function

trampoline:
    
    pushq %rbp
    movq  %rsp, %rbp

    
    // testq %rdx, %rdx
    // jz    .Lerror_stack_corrupt
    
    
    // save parameters to new stack
    movq  %rsp, -8(%rdx)
    // movq  %rdi, -16(%rdx)
    // movq  %rsi, -24(%rdx)


    // switch to new stack
    leaq  -32(%rdx), %rsp

    // test  $0xF, %rsp
    // jnz   .Lerror_stack_alignment
    
    // goto function
    movq  %rdi, %rax
    movq  %rsi, %rdi
    callq %rax
    
    // save return value to new stack
    // movq  24(%rsp), %rsp
    
    // guarantee the stack is correct
    // cmpq  %rbp, %rsp
    // jne   .Lerror_stack_corrupt
    
    movq  %rbp, %rsp
    popq  %rbp
    retq

.Lerror_stack_alignment:
    movq  $-1, %rax       # error code-1, new stack is not aligned.
    retq
.Lerror_stack_corrupt:
    movq  $-2, %rax       # error code-2, old stack is corrupted
    retq

.section .note.GNU-stack,"",@progbits