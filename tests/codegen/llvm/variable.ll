; ModuleID = 'bpftrace'
source_filename = "bpftrace"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "bpf-pc-linux"

%"struct map_t" = type { ptr, ptr, ptr, ptr }
%"struct map_t.0" = type { ptr, ptr, ptr, ptr }
%"struct map_t.1" = type { ptr, ptr }
%"struct map_t.2" = type { ptr, ptr, ptr, ptr }

@LICENSE = global [4 x i8] c"GPL\00", section "license", !dbg !0
@AT_x = dso_local global %"struct map_t" zeroinitializer, section ".maps", !dbg !7
@AT_y = dso_local global %"struct map_t.0" zeroinitializer, section ".maps", !dbg !26
@ringbuf = dso_local global %"struct map_t.1" zeroinitializer, section ".maps", !dbg !28
@event_loss_counter = dso_local global %"struct map_t.2" zeroinitializer, section ".maps", !dbg !42

; Function Attrs: nounwind
declare i64 @llvm.bpf.pseudo(i64 %0, i64 %1) #0

define i64 @kprobe_f_1(ptr %0) section "s_kprobe_f_1" !dbg !58 {
entry:
  %"@y_key" = alloca i64, align 8
  %"@x_key" = alloca i64, align 8
  %"$var" = alloca [16 x i8], align 1
  call void @llvm.lifetime.start.p0(i64 -1, ptr %"$var")
  call void @llvm.memset.p0.i64(ptr align 1 %"$var", i8 0, i64 16, i1 false)
  %comm = alloca [16 x i8], align 1
  call void @llvm.lifetime.start.p0(i64 -1, ptr %comm)
  call void @llvm.memset.p0.i64(ptr align 1 %comm, i8 0, i64 16, i1 false)
  %get_comm = call i64 inttoptr (i64 16 to ptr)(ptr %comm, i64 16)
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %"$var", ptr align 1 %comm, i64 16, i1 false)
  call void @llvm.lifetime.end.p0(i64 -1, ptr %comm)
  call void @llvm.lifetime.start.p0(i64 -1, ptr %"@x_key")
  store i64 0, ptr %"@x_key", align 8
  %update_elem = call i64 inttoptr (i64 2 to ptr)(ptr @AT_x, ptr %"@x_key", ptr %"$var", i64 0)
  call void @llvm.lifetime.end.p0(i64 -1, ptr %"@x_key")
  call void @llvm.lifetime.start.p0(i64 -1, ptr %"@y_key")
  store i64 0, ptr %"@y_key", align 8
  %update_elem1 = call i64 inttoptr (i64 2 to ptr)(ptr @AT_y, ptr %"@y_key", ptr %"$var", i64 0)
  call void @llvm.lifetime.end.p0(i64 -1, ptr %"@y_key")
  ret i64 0
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg %0, ptr nocapture %1) #1

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly %0, i8 %1, i64 %2, i1 immarg %3) #2

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly %0, ptr noalias nocapture readonly %1, i64 %2, i1 immarg %3) #3

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg %0, ptr nocapture %1) #1

attributes #0 = { nounwind }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #3 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!llvm.dbg.cu = !{!55}
!llvm.module.flags = !{!57}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "LICENSE", linkageName: "global", scope: !2, file: !2, type: !3, isLocal: false, isDefinition: true)
!2 = !DIFile(filename: "bpftrace.bpf.o", directory: ".")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 32, elements: !5)
!4 = !DIBasicType(name: "int8", size: 8, encoding: DW_ATE_signed)
!5 = !{!6}
!6 = !DISubrange(count: 4, lowerBound: 0)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(name: "AT_x", linkageName: "global", scope: !2, file: !2, type: !9, isLocal: false, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_structure_type, scope: !2, file: !2, size: 256, elements: !10)
!10 = !{!11, !17, !18, !21}
!11 = !DIDerivedType(tag: DW_TAG_member, name: "type", scope: !2, file: !2, baseType: !12, size: 64)
!12 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !13, size: 64)
!13 = !DICompositeType(tag: DW_TAG_array_type, baseType: !14, size: 32, elements: !15)
!14 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!15 = !{!16}
!16 = !DISubrange(count: 1, lowerBound: 0)
!17 = !DIDerivedType(tag: DW_TAG_member, name: "max_entries", scope: !2, file: !2, baseType: !12, size: 64, offset: 64)
!18 = !DIDerivedType(tag: DW_TAG_member, name: "key", scope: !2, file: !2, baseType: !19, size: 64, offset: 128)
!19 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !20, size: 64)
!20 = !DIBasicType(name: "int64", size: 64, encoding: DW_ATE_signed)
!21 = !DIDerivedType(tag: DW_TAG_member, name: "value", scope: !2, file: !2, baseType: !22, size: 64, offset: 192)
!22 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !23, size: 64)
!23 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 128, elements: !24)
!24 = !{!25}
!25 = !DISubrange(count: 16, lowerBound: 0)
!26 = !DIGlobalVariableExpression(var: !27, expr: !DIExpression())
!27 = distinct !DIGlobalVariable(name: "AT_y", linkageName: "global", scope: !2, file: !2, type: !9, isLocal: false, isDefinition: true)
!28 = !DIGlobalVariableExpression(var: !29, expr: !DIExpression())
!29 = distinct !DIGlobalVariable(name: "ringbuf", linkageName: "global", scope: !2, file: !2, type: !30, isLocal: false, isDefinition: true)
!30 = !DICompositeType(tag: DW_TAG_structure_type, scope: !2, file: !2, size: 128, elements: !31)
!31 = !{!32, !37}
!32 = !DIDerivedType(tag: DW_TAG_member, name: "type", scope: !2, file: !2, baseType: !33, size: 64)
!33 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !34, size: 64)
!34 = !DICompositeType(tag: DW_TAG_array_type, baseType: !14, size: 864, elements: !35)
!35 = !{!36}
!36 = !DISubrange(count: 27, lowerBound: 0)
!37 = !DIDerivedType(tag: DW_TAG_member, name: "max_entries", scope: !2, file: !2, baseType: !38, size: 64, offset: 64)
!38 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !39, size: 64)
!39 = !DICompositeType(tag: DW_TAG_array_type, baseType: !14, size: 8388608, elements: !40)
!40 = !{!41}
!41 = !DISubrange(count: 262144, lowerBound: 0)
!42 = !DIGlobalVariableExpression(var: !43, expr: !DIExpression())
!43 = distinct !DIGlobalVariable(name: "event_loss_counter", linkageName: "global", scope: !2, file: !2, type: !44, isLocal: false, isDefinition: true)
!44 = !DICompositeType(tag: DW_TAG_structure_type, scope: !2, file: !2, size: 256, elements: !45)
!45 = !{!46, !17, !51, !54}
!46 = !DIDerivedType(tag: DW_TAG_member, name: "type", scope: !2, file: !2, baseType: !47, size: 64)
!47 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !48, size: 64)
!48 = !DICompositeType(tag: DW_TAG_array_type, baseType: !14, size: 64, elements: !49)
!49 = !{!50}
!50 = !DISubrange(count: 2, lowerBound: 0)
!51 = !DIDerivedType(tag: DW_TAG_member, name: "key", scope: !2, file: !2, baseType: !52, size: 64, offset: 128)
!52 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !53, size: 64)
!53 = !DIBasicType(name: "int32", size: 32, encoding: DW_ATE_signed)
!54 = !DIDerivedType(tag: DW_TAG_member, name: "value", scope: !2, file: !2, baseType: !19, size: 64, offset: 192)
!55 = distinct !DICompileUnit(language: DW_LANG_C, file: !2, producer: "bpftrace", isOptimized: false, runtimeVersion: 0, emissionKind: LineTablesOnly, globals: !56)
!56 = !{!0, !7, !26, !28, !42}
!57 = !{i32 2, !"Debug Info Version", i32 3}
!58 = distinct !DISubprogram(name: "kprobe_f_1", linkageName: "kprobe_f_1", scope: !2, file: !2, type: !59, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !55, retainedNodes: !62)
!59 = !DISubroutineType(types: !60)
!60 = !{!20, !61}
!61 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!62 = !{!63}
!63 = !DILocalVariable(name: "ctx", arg: 1, scope: !58, file: !2, type: !61)
