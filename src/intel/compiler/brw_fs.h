/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#ifndef BRW_FS_H
#define BRW_FS_H

#include "brw_shader.h"
#include "brw_ir_fs.h"
#include "brw_fs_builder.h"
#include "brw_fs_live_variables.h"
#include "brw_ir_performance.h"
#include "compiler/nir/nir.h"

struct bblock_t;
namespace {
   struct acp_entry;
}

class fs_visitor;

namespace brw {
   /**
    * Register pressure analysis of a shader.  Estimates how many registers
    * are live at any point of the program in GRF units.
    */
   struct register_pressure {
      register_pressure(const fs_visitor *v);
      ~register_pressure();

      analysis_dependency_class
      dependency_class() const
      {
         return (DEPENDENCY_INSTRUCTION_IDENTITY |
                 DEPENDENCY_INSTRUCTION_DATA_FLOW |
                 DEPENDENCY_VARIABLES);
      }

      bool
      validate(const fs_visitor *) const
      {
         /* FINISHME */
         return true;
      }

      unsigned *regs_live_at_ip;
   };
}

struct brw_gs_compile;

static inline fs_reg
offset(const fs_reg &reg, const brw::fs_builder &bld, unsigned delta)
{
   return offset(reg, bld.dispatch_width(), delta);
}

struct shader_stats {
   const char *scheduler_mode;
   unsigned promoted_constants;
   unsigned spill_count;
   unsigned fill_count;
   unsigned max_register_pressure;
};

/** Register numbers for thread payload fields. */
struct thread_payload {
   /** The number of thread payload registers the hardware will supply. */
   uint8_t num_regs;

   virtual ~thread_payload() = default;

protected:
   thread_payload() : num_regs() {}
};

struct vs_thread_payload : public thread_payload {
   vs_thread_payload();

   fs_reg urb_handles;
};

struct tcs_thread_payload : public thread_payload {
   tcs_thread_payload(const fs_visitor &v);

   fs_reg patch_urb_output;
   fs_reg primitive_id;
   fs_reg icp_handle_start;
};

struct tes_thread_payload : public thread_payload {
   tes_thread_payload();

   fs_reg patch_urb_input;
   fs_reg primitive_id;
   fs_reg coords[3];
   fs_reg urb_output;
};

struct gs_thread_payload : public thread_payload {
   gs_thread_payload(const fs_visitor &v);

   fs_reg urb_handles;
   fs_reg primitive_id;
   fs_reg icp_handle_start;
};

struct fs_thread_payload : public thread_payload {
   fs_thread_payload(const fs_visitor &v,
                     bool &source_depth_to_render_target,
                     bool &runtime_check_aads_emit);

   uint8_t subspan_coord_reg[2];
   uint8_t source_depth_reg[2];
   uint8_t source_w_reg[2];
   uint8_t aa_dest_stencil_reg[2];
   uint8_t dest_depth_reg[2];
   uint8_t sample_pos_reg[2];
   uint8_t sample_mask_in_reg[2];
   uint8_t depth_w_coef_reg[2];
   uint8_t barycentric_coord_reg[BRW_BARYCENTRIC_MODE_COUNT][2];
};

struct cs_thread_payload : public thread_payload {
   cs_thread_payload(const fs_visitor &v);

   void load_subgroup_id(const brw::fs_builder &bld, fs_reg &dest) const;

protected:
   fs_reg subgroup_id_;
};

struct task_mesh_thread_payload : public cs_thread_payload {
   task_mesh_thread_payload(const fs_visitor &v);

   fs_reg extended_parameter_0;
   fs_reg local_index;
   fs_reg inline_parameter;

   fs_reg urb_output;

   /* URB to read Task memory inputs. Only valid for MESH stage. */
   fs_reg task_urb_input;
};

struct bs_thread_payload : public thread_payload {
   bs_thread_payload();

   fs_reg global_arg_ptr;
   fs_reg local_arg_ptr;

   void load_shader_type(const brw::fs_builder &bld, fs_reg &dest) const;
};

struct brw_fs_bind_info {
   bool valid;
   bool bindless;
   unsigned block;
   unsigned set;
   unsigned binding;
};

/**
 * The fragment shader front-end.
 *
 * Translates either GLSL IR or Mesa IR (for ARB_fragment_program) into FS IR.
 */
class fs_visitor : public backend_shader
{
public:
   fs_visitor(const struct brw_compiler *compiler,
              const struct brw_compile_params *params,
              const brw_base_prog_key *key,
              struct brw_stage_prog_data *prog_data,
              const nir_shader *shader,
              unsigned dispatch_width,
              bool needs_register_pressure,
              bool debug_enabled);
   fs_visitor(const struct brw_compiler *compiler,
              const struct brw_compile_params *params,
              struct brw_gs_compile *gs_compile,
              struct brw_gs_prog_data *prog_data,
              const nir_shader *shader,
              bool needs_register_pressure,
              bool debug_enabled);
   void init();
   ~fs_visitor();

   fs_reg vgrf(const glsl_type *const type);
   void import_uniforms(fs_visitor *v);

   void VARYING_PULL_CONSTANT_LOAD(const brw::fs_builder &bld,
                                   const fs_reg &dst,
                                   const fs_reg &surface,
                                   const fs_reg &surface_handle,
                                   const fs_reg &varying_offset,
                                   uint32_t const_offset,
                                   uint8_t alignment);
   void DEP_RESOLVE_MOV(const brw::fs_builder &bld, int grf);

   bool run_fs(bool allow_spilling, bool do_rep_send);
   bool run_vs();
   bool run_tcs();
   bool run_tes();
   bool run_gs();
   bool run_cs(bool allow_spilling);
   bool run_bs(bool allow_spilling);
   bool run_task(bool allow_spilling);
   bool run_mesh(bool allow_spilling);
   void optimize();
   void allocate_registers(bool allow_spilling);
   uint32_t compute_max_register_pressure();
   bool fixup_sends_duplicate_payload();
   void fixup_3src_null_dest();
   void emit_dummy_memory_fence_before_eot();
   void emit_dummy_mov_instruction();
   bool fixup_nomask_control_flow();
   void assign_curb_setup();
   void assign_urb_setup();
   void convert_attr_sources_to_hw_regs(fs_inst *inst);
   void assign_vs_urb_setup();
   void assign_tcs_urb_setup();
   void assign_tes_urb_setup();
   void assign_gs_urb_setup();
   bool assign_regs(bool allow_spilling, bool spill_all);
   void assign_regs_trivial();
   void calculate_payload_ranges(int payload_node_count,
                                 int *payload_last_use_ip) const;
   bool split_virtual_grfs();
   bool compact_virtual_grfs();
   void assign_constant_locations();
   bool get_pull_locs(const fs_reg &src, unsigned *out_surf_index,
                      unsigned *out_pull_index);
   void lower_constant_loads();
   virtual void invalidate_analysis(brw::analysis_dependency_class c);
   void validate();
   bool opt_algebraic();
   bool opt_redundant_halt();
   bool opt_cse();
   bool opt_cse_local(const brw::fs_live_variables &live, bblock_t *block, int &ip);

   bool opt_copy_propagation();
   bool try_copy_propagate(fs_inst *inst, int arg, acp_entry *entry);
   bool try_constant_propagate(fs_inst *inst, acp_entry *entry);
   bool opt_copy_propagation_local(void *mem_ctx, bblock_t *block,
                                   exec_list *acp);
   bool opt_register_renaming();
   bool opt_bank_conflicts();
   bool opt_split_sends();
   bool register_coalesce();
   bool compute_to_mrf();
   bool eliminate_find_live_channel();
   bool dead_code_eliminate();
   bool remove_duplicate_mrf_writes();
   bool remove_extra_rounding_modes();

   void schedule_instructions(instruction_scheduler_mode mode);
   void insert_gfx4_send_dependency_workarounds();
   void insert_gfx4_pre_send_dependency_workarounds(bblock_t *block,
                                                    fs_inst *inst);
   void insert_gfx4_post_send_dependency_workarounds(bblock_t *block,
                                                     fs_inst *inst);
   void vfail(const char *msg, va_list args);
   void fail(const char *msg, ...);
   void limit_dispatch_width(unsigned n, const char *msg);
   void lower_uniform_pull_constant_loads();
   bool lower_load_payload();
   bool lower_pack();
   bool lower_regioning();
   bool lower_logical_sends();
   bool lower_integer_multiplication();
   bool lower_minmax();
   bool lower_simd_width();
   bool lower_barycentrics();
   bool lower_derivatives();
   bool lower_find_live_channel();
   bool lower_scoreboard();
   bool lower_sub_sat();
   bool opt_combine_constants();

   void emit_dummy_fs();
   void emit_repclear_shader();
   void emit_fragcoord_interpolation(fs_reg wpos);
   void emit_is_helper_invocation(fs_reg result);
   fs_reg emit_frontfacing_interpolation();
   fs_reg emit_samplepos_setup();
   fs_reg emit_sampleid_setup();
   fs_reg emit_samplemaskin_setup();
   fs_reg emit_shading_rate_setup();
   void emit_interpolation_setup_gfx4();
   void emit_interpolation_setup_gfx6();
   fs_reg emit_mcs_fetch(const fs_reg &coordinate, unsigned components,
                         const fs_reg &texture,
                         const fs_reg &texture_handle);
   fs_reg resolve_source_modifiers(const brw::fs_builder &bld, const fs_reg &src);
   void emit_fsign(const class brw::fs_builder &, const nir_alu_instr *instr,
                   fs_reg result, fs_reg *op, unsigned fsign_src);
   void emit_shader_float_controls_execution_mode();
   bool opt_peephole_sel();
   bool opt_saturate_propagation();
   bool opt_cmod_propagation();
   bool opt_zero_samples();

   void set_tcs_invocation_id();

   void emit_nir_code();
   void nir_setup_outputs();
   void nir_setup_uniforms();
   void nir_emit_system_values();
   void nir_emit_impl(nir_function_impl *impl);
   void nir_emit_cf_list(exec_list *list);
   void nir_emit_if(nir_if *if_stmt);
   void nir_emit_loop(nir_loop *loop);
   void nir_emit_block(nir_block *block);
   void nir_emit_instr(nir_instr *instr);
   void nir_emit_alu(const brw::fs_builder &bld, nir_alu_instr *instr,
                     bool need_dest);
   bool try_emit_b2fi_of_inot(const brw::fs_builder &bld, fs_reg result,
                              nir_alu_instr *instr);
   void nir_emit_load_const(const brw::fs_builder &bld,
                            nir_load_const_instr *instr);
   void nir_emit_vs_intrinsic(const brw::fs_builder &bld,
                              nir_intrinsic_instr *instr);
   void nir_emit_tcs_intrinsic(const brw::fs_builder &bld,
                               nir_intrinsic_instr *instr);
   void nir_emit_gs_intrinsic(const brw::fs_builder &bld,
                              nir_intrinsic_instr *instr);
   void nir_emit_fs_intrinsic(const brw::fs_builder &bld,
                              nir_intrinsic_instr *instr);
   void nir_emit_cs_intrinsic(const brw::fs_builder &bld,
                              nir_intrinsic_instr *instr);
   void nir_emit_bs_intrinsic(const brw::fs_builder &bld,
                              nir_intrinsic_instr *instr);
   void nir_emit_task_intrinsic(const brw::fs_builder &bld,
                                nir_intrinsic_instr *instr);
   void nir_emit_mesh_intrinsic(const brw::fs_builder &bld,
                                nir_intrinsic_instr *instr);
   void nir_emit_task_mesh_intrinsic(const brw::fs_builder &bld,
                                     nir_intrinsic_instr *instr);
   fs_reg get_nir_image_intrinsic_image(const brw::fs_builder &bld,
                                        nir_intrinsic_instr *instr);
   fs_reg get_nir_buffer_intrinsic_index(const brw::fs_builder &bld,
                                         nir_intrinsic_instr *instr);
   fs_reg swizzle_nir_scratch_addr(const brw::fs_builder &bld,
                                   const fs_reg &addr,
                                   bool in_dwords);
   void nir_emit_intrinsic(const brw::fs_builder &bld,
                           nir_intrinsic_instr *instr);
   void nir_emit_tes_intrinsic(const brw::fs_builder &bld,
                               nir_intrinsic_instr *instr);
   void nir_emit_surface_atomic(const brw::fs_builder &bld,
                                nir_intrinsic_instr *instr,
                                fs_reg surface,
                                bool bindless_surface);
   void nir_emit_global_atomic(const brw::fs_builder &bld,
                               nir_intrinsic_instr *instr);
   void nir_emit_texture(const brw::fs_builder &bld,
                         nir_tex_instr *instr);
   void nir_emit_jump(const brw::fs_builder &bld,
                      nir_jump_instr *instr);
   bool get_nir_src_bindless(const nir_src &src);
   unsigned get_nir_src_block(const nir_src &src);
   fs_reg get_nir_src(const nir_src &src);
   fs_reg get_nir_src_imm(const nir_src &src);
   fs_reg get_nir_dest(const nir_dest &dest);
   nir_component_mask_t get_nir_write_mask(const nir_alu_dest &dest);
   fs_reg get_resource_nir_src(const nir_src &src);
   fs_reg try_rebuild_resource(const brw::fs_builder &bld,
                               nir_ssa_def *resource_def);
   fs_reg get_indirect_offset(nir_intrinsic_instr *instr);
   fs_reg get_tcs_single_patch_icp_handle(const brw::fs_builder &bld,
                                          nir_intrinsic_instr *instr);
   fs_reg get_tcs_multi_patch_icp_handle(const brw::fs_builder &bld,
                                         nir_intrinsic_instr *instr);

   bool optimize_extract_to_float(nir_alu_instr *instr,
                                  const fs_reg &result);
   bool optimize_frontfacing_ternary(nir_alu_instr *instr,
                                     const fs_reg &result);

   void emit_alpha_test();
   fs_inst *emit_single_fb_write(const brw::fs_builder &bld,
                                 fs_reg color1, fs_reg color2,
                                 fs_reg src0_alpha, unsigned components);
   void do_emit_fb_writes(int nr_color_regions, bool replicate_alpha);
   void emit_fb_writes();
   fs_inst *emit_non_coherent_fb_read(const brw::fs_builder &bld,
                                      const fs_reg &dst, unsigned target);
   void emit_urb_writes(const fs_reg &gs_vertex_count = fs_reg());
   void set_gs_stream_control_data_bits(const fs_reg &vertex_count,
                                        unsigned stream_id);
   void emit_gs_control_data_bits(const fs_reg &vertex_count);
   void emit_gs_end_primitive(const nir_src &vertex_count_nir_src);
   void emit_gs_vertex(const nir_src &vertex_count_nir_src,
                       unsigned stream_id);
   void emit_gs_thread_end();
   void emit_gs_input_load(const fs_reg &dst, const nir_src &vertex_src,
                           unsigned base_offset, const nir_src &offset_src,
                           unsigned num_components, unsigned first_component);
   bool mark_last_urb_write_with_eot();
   void emit_tcs_thread_end();
   void emit_urb_fence();
   void emit_cs_terminate();
   fs_reg emit_work_group_id_setup();

   void emit_task_mesh_store(const brw::fs_builder &bld,
                             nir_intrinsic_instr *instr,
                             const fs_reg &urb_handle);
   void emit_task_mesh_load(const brw::fs_builder &bld,
                            nir_intrinsic_instr *instr,
                            const fs_reg &urb_handle);

   void emit_barrier();
   void emit_tcs_barrier();

   fs_reg get_timestamp(const brw::fs_builder &bld);

   fs_reg interp_reg(int location, int channel);
   fs_reg per_primitive_reg(int location, unsigned comp);

   virtual void dump_instruction_to_file(const backend_instruction *inst, FILE *file) const;
   virtual void dump_instructions_to_file(FILE *file) const;

   const brw_base_prog_key *const key;
   const struct brw_sampler_prog_key_data *key_tex;

   struct brw_gs_compile *gs_compile;

   struct brw_stage_prog_data *prog_data;

   brw_analysis<brw::fs_live_variables, backend_shader> live_analysis;
   brw_analysis<brw::register_pressure, fs_visitor> regpressure_analysis;
   brw_analysis<brw::performance, fs_visitor> performance_analysis;

   /** Number of uniform variable components visited. */
   unsigned uniforms;

   /** Byte-offset for the next available spot in the scratch space buffer. */
   unsigned last_scratch;

   /**
    * Array mapping UNIFORM register numbers to the push parameter index,
    * or -1 if this uniform register isn't being uploaded as a push constant.
    */
   int *push_constant_loc;

   fs_reg frag_depth;
   fs_reg frag_stencil;
   fs_reg sample_mask;
   fs_reg outputs[VARYING_SLOT_MAX];
   fs_reg dual_src_output;
   int first_non_payload_grf;
   /** Either BRW_MAX_GRF or GFX7_MRF_HACK_START */
   unsigned max_grf;

   fs_reg *nir_ssa_values;
   fs_inst **nir_resource_insts;
   struct brw_fs_bind_info *nir_ssa_bind_infos;
   fs_reg *nir_resource_values;
   fs_reg *nir_system_values;

   bool failed;
   char *fail_msg;

   thread_payload *payload_;

   thread_payload &payload() {
      return *this->payload_;
   }

   vs_thread_payload &vs_payload() {
      assert(stage == MESA_SHADER_VERTEX);
      return *static_cast<vs_thread_payload *>(this->payload_);
   }

   tcs_thread_payload &tcs_payload() {
      assert(stage == MESA_SHADER_TESS_CTRL);
      return *static_cast<tcs_thread_payload *>(this->payload_);
   }

   tes_thread_payload &tes_payload() {
      assert(stage == MESA_SHADER_TESS_EVAL);
      return *static_cast<tes_thread_payload *>(this->payload_);
   }

   gs_thread_payload &gs_payload() {
      assert(stage == MESA_SHADER_GEOMETRY);
      return *static_cast<gs_thread_payload *>(this->payload_);
   }

   fs_thread_payload &fs_payload() {
      assert(stage == MESA_SHADER_FRAGMENT);
      return *static_cast<fs_thread_payload *>(this->payload_);
   };

   cs_thread_payload &cs_payload() {
      assert(gl_shader_stage_uses_workgroup(stage));
      return *static_cast<cs_thread_payload *>(this->payload_);
   }

   task_mesh_thread_payload &task_mesh_payload() {
      assert(stage == MESA_SHADER_TASK || stage == MESA_SHADER_MESH);
      return *static_cast<task_mesh_thread_payload *>(this->payload_);
   }

   bs_thread_payload &bs_payload() {
      assert(stage >= MESA_SHADER_RAYGEN && stage <= MESA_SHADER_CALLABLE);
      return *static_cast<bs_thread_payload *>(this->payload_);
   }

   bool source_depth_to_render_target;
   bool runtime_check_aads_emit;

   fs_reg pixel_x;
   fs_reg pixel_y;
   fs_reg pixel_z;
   fs_reg wpos_w;
   fs_reg pixel_w;
   fs_reg delta_xy[BRW_BARYCENTRIC_MODE_COUNT];
   fs_reg final_gs_vertex_count;
   fs_reg control_data_bits;
   fs_reg invocation_id;

   unsigned grf_used;
   bool spilled_any_registers;
   bool needs_register_pressure;

   const unsigned dispatch_width; /**< 8, 16 or 32 */
   unsigned max_dispatch_width;

   /* The API selected subgroup size */
   unsigned api_subgroup_size; /**< 0, 8, 16, 32 */

   struct shader_stats shader_stats;

   brw::fs_builder bld;

private:
   fs_reg prepare_alu_destination_and_sources(const brw::fs_builder &bld,
                                              nir_alu_instr *instr,
                                              fs_reg *op,
                                              bool need_dest);

   void resolve_inot_sources(const brw::fs_builder &bld, nir_alu_instr *instr,
                             fs_reg *op);
   void lower_mul_dword_inst(fs_inst *inst, bblock_t *block);
   void lower_mul_qword_inst(fs_inst *inst, bblock_t *block);
   void lower_mulh_inst(fs_inst *inst, bblock_t *block);

   unsigned workgroup_size() const;
};

/**
 * Return the flag register used in fragment shaders to keep track of live
 * samples.  On Gfx7+ we use f1.0-f1.1 to allow discard jumps in SIMD32
 * dispatch mode, while earlier generations are constrained to f0.1, which
 * limits the dispatch width to SIMD16 for fragment shaders that use discard.
 */
static inline unsigned
sample_mask_flag_subreg(const fs_visitor *shader)
{
   assert(shader->stage == MESA_SHADER_FRAGMENT);
   return shader->devinfo->ver >= 7 ? 2 : 1;
}

/**
 * The fragment shader code generator.
 *
 * Translates FS IR to actual i965 assembly code.
 */
class fs_generator
{
public:
   fs_generator(const struct brw_compiler *compiler,
                const struct brw_compile_params *params,
                struct brw_stage_prog_data *prog_data,
                bool runtime_check_aads_emit,
                gl_shader_stage stage);
   ~fs_generator();

   void enable_debug(const char *shader_name);
   int generate_code(const cfg_t *cfg, int dispatch_width,
                     struct shader_stats shader_stats,
                     const brw::performance &perf,
                     struct brw_compile_stats *stats);
   void add_const_data(void *data, unsigned size);
   void add_resume_sbt(unsigned num_resume_shaders, uint64_t *sbt);
   const unsigned *get_assembly();

private:
   void fire_fb_write(fs_inst *inst,
                      struct brw_reg payload,
                      struct brw_reg implied_header,
                      GLuint nr);
   void generate_send(fs_inst *inst,
                      struct brw_reg dst,
                      struct brw_reg desc,
                      struct brw_reg ex_desc,
                      struct brw_reg payload,
                      struct brw_reg payload2);
   void generate_fb_write(fs_inst *inst, struct brw_reg payload);
   void generate_fb_read(fs_inst *inst, struct brw_reg dst,
                         struct brw_reg payload);
   void generate_cs_terminate(fs_inst *inst, struct brw_reg payload);
   void generate_barrier(fs_inst *inst, struct brw_reg src);
   bool generate_linterp(fs_inst *inst, struct brw_reg dst,
			 struct brw_reg *src);
   void generate_tex(fs_inst *inst, struct brw_reg dst,
                     struct brw_reg surface_index,
                     struct brw_reg sampler_index);
   void generate_ddx(const fs_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_ddy(const fs_inst *inst,
                     struct brw_reg dst, struct brw_reg src);
   void generate_scratch_write(fs_inst *inst, struct brw_reg src);
   void generate_scratch_read(fs_inst *inst, struct brw_reg dst);
   void generate_scratch_read_gfx7(fs_inst *inst, struct brw_reg dst);
   void generate_scratch_header(fs_inst *inst, struct brw_reg dst);
   void generate_uniform_pull_constant_load(fs_inst *inst, struct brw_reg dst,
                                            struct brw_reg index,
                                            struct brw_reg offset);
   void generate_varying_pull_constant_load_gfx4(fs_inst *inst,
                                                 struct brw_reg dst,
                                                 struct brw_reg index);

   void generate_set_sample_id(fs_inst *inst,
                               struct brw_reg dst,
                               struct brw_reg src0,
                               struct brw_reg src1);

   void generate_halt(fs_inst *inst);

   void generate_mov_indirect(fs_inst *inst,
                              struct brw_reg dst,
                              struct brw_reg reg,
                              struct brw_reg indirect_byte_offset);

   void generate_shuffle(fs_inst *inst,
                         struct brw_reg dst,
                         struct brw_reg src,
                         struct brw_reg idx);

   void generate_quad_swizzle(const fs_inst *inst,
                              struct brw_reg dst, struct brw_reg src,
                              unsigned swiz);

   bool patch_halt_jumps();

   const struct brw_compiler *compiler;
   const struct brw_compile_params *params;

   const struct intel_device_info *devinfo;

   struct brw_codegen *p;
   struct brw_stage_prog_data * const prog_data;

   unsigned dispatch_width; /**< 8, 16 or 32 */

   exec_list discard_halt_patches;
   bool runtime_check_aads_emit;
   bool debug_flag;
   const char *shader_name;
   gl_shader_stage stage;
   void *mem_ctx;
};

namespace brw {
   inline fs_reg
   fetch_payload_reg(const brw::fs_builder &bld, uint8_t regs[2],
                     brw_reg_type type = BRW_REGISTER_TYPE_F)
   {
      if (!regs[0])
         return fs_reg();

      if (bld.dispatch_width() > 16) {
         const fs_reg tmp = bld.vgrf(type);
         const brw::fs_builder hbld = bld.exec_all().group(16, 0);
         const unsigned m = bld.dispatch_width() / hbld.dispatch_width();
         fs_reg components[2];
         assert(m <= 2);

         for (unsigned g = 0; g < m; g++)
               components[g] = retype(brw_vec8_grf(regs[g], 0), type);

         hbld.LOAD_PAYLOAD(tmp, components, m, 0);

         return tmp;

      } else {
         return fs_reg(retype(brw_vec8_grf(regs[0], 0), type));
      }
   }

   inline fs_reg
   fetch_barycentric_reg(const brw::fs_builder &bld, uint8_t regs[2])
   {
      if (!regs[0])
         return fs_reg();

      const fs_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_F, 2);
      const brw::fs_builder hbld = bld.exec_all().group(8, 0);
      const unsigned m = bld.dispatch_width() / hbld.dispatch_width();
      fs_reg *const components = new fs_reg[2 * m];

      for (unsigned c = 0; c < 2; c++) {
         for (unsigned g = 0; g < m; g++)
            components[c * m + g] = offset(brw_vec8_grf(regs[g / 2], 0),
                                           hbld, c + 2 * (g % 2));
      }

      hbld.LOAD_PAYLOAD(tmp, components, 2 * m, 0);

      delete[] components;
      return tmp;
   }

   inline fs_reg
   dynamic_msaa_flags(const struct brw_wm_prog_data *wm_prog_data)
   {
      return fs_reg(UNIFORM, wm_prog_data->msaa_flags_param,
                    BRW_REGISTER_TYPE_UD);
   }

   inline void
   check_dynamic_msaa_flag(const fs_builder &bld,
                           const struct brw_wm_prog_data *wm_prog_data,
                           enum brw_wm_msaa_flags flag)
   {
      fs_inst *inst = bld.AND(bld.null_reg_ud(),
                              dynamic_msaa_flags(wm_prog_data),
                              brw_imm_ud(flag));
      inst->conditional_mod = BRW_CONDITIONAL_NZ;
   }

   bool
   lower_src_modifiers(fs_visitor *v, bblock_t *block, fs_inst *inst, unsigned i);
}

void shuffle_from_32bit_read(const brw::fs_builder &bld,
                             const fs_reg &dst,
                             const fs_reg &src,
                             uint32_t first_component,
                             uint32_t components);

fs_reg setup_imm_df(const brw::fs_builder &bld,
                    double v);

fs_reg setup_imm_b(const brw::fs_builder &bld,
                   int8_t v);

fs_reg setup_imm_ub(const brw::fs_builder &bld,
                   uint8_t v);

enum brw_barycentric_mode brw_barycentric_mode(nir_intrinsic_instr *intr);

uint32_t brw_fb_write_msg_control(const fs_inst *inst,
                                  const struct brw_wm_prog_data *prog_data);

void brw_compute_urb_setup_index(struct brw_wm_prog_data *wm_prog_data);

bool brw_nir_lower_simd(nir_shader *nir, unsigned dispatch_width);

fs_reg brw_sample_mask_reg(const brw::fs_builder &bld);
void brw_emit_predicate_on_sample_mask(const brw::fs_builder &bld, fs_inst *inst);

int brw_get_subgroup_id_param_index(const intel_device_info *devinfo,
                                    const brw_stage_prog_data *prog_data);


#endif /* BRW_FS_H */
