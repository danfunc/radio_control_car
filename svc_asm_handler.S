.cpu cortex-m33
.thumb
.thumb_func
.global svc_asm_handler
.align 8
.type svc_arm_handler, %function
svc_asm_handler:
push {lr}
bl get_current_context
stmia r0!,{r4-r7}
mov r1,r8
mov r2,r9
mov r3,r10
mov r4,r11
mrs r5,PSP
stmia r0!,{r1-r5}
add r0, #-36
bl svc_cpp_handler
bl get_current_context
ldmia r0!,{r4-r7}
ldmia r0!,{r1-r3}
mov r8,r1
mov r9,r2
mov r10,r3
ldmia r0!,{r1-r2}
mov r11,r1
msr PSP,r2
pop {pc}
