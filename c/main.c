#include <stdio.h>
#include "llvm.h"

int main() {
	sm_jit_init ();
	
	const char* code = "\n\
; ModuleID = 'hello.c'\n\
target datalayout = \"e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128\"\n\
target triple = \"x86_64-pc-linux-gnu\"\n\
\n\
@.str = private unnamed_addr constant [7 x i8] c\"hello\\0A\\00\", align 1\n\
\n\
; Function Attrs: nounwind uwtable\n\
define void @_smc_main() #0 {\n\
  %1 = call i32 (i8*, ...)* @printf(i8* getelementptr inbounds ([7 x i8]* @.str, i32 0, i32 0))\n\
  ret void\n\
}\n\
\n\
declare i32 @printf(i8*, ...) #1\n\
\n\
attributes #0 = { nounwind uwtable \"less-precise-fpmad\"=\"false\" \"no-frame-pointer-elim\"=\"true\" \"no-frame-pointer-elim-non-leaf\"=\"true\" \"no-infs-fp-math\"=\"false\" \"no-nans-fp-math\"=\"false\" \"unsafe-fp-math\"=\"false\" \"use-soft-float\"=\"false\" }\n\
attributes #1 = { \"less-precise-fpmad\"=\"false\" \"no-frame-pointer-elim\"=\"true\" \"no-frame-pointer-elim-non-leaf\"=\"true\" \"no-infs-fp-math\"=\"false\" \"no-nans-fp-math\"=\"false\" \"unsafe-fp-math\"=\"false\" \"use-soft-float\"=\"false\" }\n\
	";

	SmJit* mod = sm_jit_compile ("<stdin>", code);
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, "_smc_main");
	entrypoint();
}