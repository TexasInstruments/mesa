use mesa_rust_gen::*;
use mesa_rust_util::bitset;
use mesa_rust_util::offset_of;

use std::convert::TryInto;
use std::ffi::c_void;
use std::ffi::CString;
use std::marker::PhantomData;
use std::ptr;
use std::ptr::NonNull;
use std::slice;

pub struct ExecListIter<'a, T> {
    n: &'a mut exec_node,
    offset: usize,
    _marker: PhantomData<T>,
}

impl<'a, T> ExecListIter<'a, T> {
    fn new(l: &'a mut exec_list, offset: usize) -> Self {
        Self {
            n: &mut l.head_sentinel,
            offset: offset,
            _marker: PhantomData,
        }
    }
}

impl<'a, T: 'a> Iterator for ExecListIter<'a, T> {
    type Item = &'a mut T;

    fn next(&mut self) -> Option<Self::Item> {
        self.n = unsafe { &mut *self.n.next };
        if self.n.next.is_null() {
            None
        } else {
            let t: *mut c_void = (self.n as *mut exec_node).cast();
            Some(unsafe { &mut *(t.sub(self.offset).cast()) })
        }
    }
}

pub struct NirPrintfInfo {
    count: usize,
    printf_info: *mut u_printf_info,
}

impl NirPrintfInfo {
    pub fn u_printf(&self, buf: &[u8]) {
        unsafe {
            u_printf(
                stdout_ptr(),
                buf.as_ptr().cast(),
                buf.len(),
                self.printf_info.cast(),
                self.count as u32,
            );
        }
    }
}

impl Drop for NirPrintfInfo {
    fn drop(&mut self) {
        unsafe {
            ralloc_free(self.printf_info.cast());
        };
    }
}

pub struct NirShader {
    nir: NonNull<nir_shader>,
}

impl NirShader {
    pub fn new(nir: *mut nir_shader) -> Option<Self> {
        NonNull::new(nir).map(|nir| Self { nir: nir })
    }

    pub fn deserialize(
        input: &mut &[u8],
        len: usize,
        options: *const nir_shader_compiler_options,
    ) -> Option<Self> {
        let mut reader = blob_reader::default();

        let (bin, rest) = input.split_at(len);
        *input = rest;

        unsafe {
            blob_reader_init(&mut reader, bin.as_ptr().cast(), len);
            Self::new(nir_deserialize(ptr::null_mut(), options, &mut reader))
        }
    }

    pub fn serialize(&self) -> Vec<u8> {
        let mut blob = blob::default();
        unsafe {
            blob_init(&mut blob);
            nir_serialize(&mut blob, self.nir.as_ptr(), false);
            let res = slice::from_raw_parts(blob.data, blob.size).to_vec();
            blob_finish(&mut blob);
            res
        }
    }

    pub fn print(&self) {
        unsafe { nir_print_shader(self.nir.as_ptr(), stderr_ptr()) };
    }

    pub fn get_nir(&self) -> *mut nir_shader {
        self.nir.as_ptr()
    }

    pub fn dup_for_driver(&self) -> *mut nir_shader {
        unsafe { nir_shader_clone(ptr::null_mut(), self.nir.as_ptr()) }
    }

    pub fn sweep_mem(&self) {
        unsafe { nir_sweep(self.nir.as_ptr()) }
    }

    pub fn pass0<R>(&mut self, pass: unsafe extern "C" fn(*mut nir_shader) -> R) -> R {
        unsafe { pass(self.nir.as_ptr()) }
    }

    pub fn pass1<R, A>(
        &mut self,
        pass: unsafe extern "C" fn(*mut nir_shader, a: A) -> R,
        a: A,
    ) -> R {
        unsafe { pass(self.nir.as_ptr(), a) }
    }

    pub fn pass2<R, A, B>(
        &mut self,
        pass: unsafe extern "C" fn(*mut nir_shader, a: A, b: B) -> R,
        a: A,
        b: B,
    ) -> R {
        unsafe { pass(self.nir.as_ptr(), a, b) }
    }

    pub fn pass3<R, A, B, C>(
        &mut self,
        pass: unsafe extern "C" fn(*mut nir_shader, a: A, b: B, c: C) -> R,
        a: A,
        b: B,
        c: C,
    ) -> R {
        unsafe { pass(self.nir.as_ptr(), a, b, c) }
    }

    pub fn entrypoint(&self) -> *mut nir_function_impl {
        unsafe { nir_shader_get_entrypoint(self.nir.as_ptr()) }
    }

    pub fn structurize(&mut self) {
        self.pass0(nir_lower_goto_ifs);
        self.pass0(nir_opt_dead_cf);
    }

    pub fn inline(&mut self, libclc: &NirShader) {
        self.pass1(
            nir_lower_variable_initializers,
            nir_variable_mode::nir_var_function_temp,
        );
        self.pass0(nir_lower_returns);
        self.pass1(nir_lower_libclc, libclc.nir.as_ptr());
        self.pass0(nir_inline_functions);
    }

    pub fn remove_non_entrypoints(&mut self) {
        unsafe { nir_remove_non_entrypoints(self.nir.as_ptr()) };
    }

    pub fn variables(&mut self) -> ExecListIter<nir_variable> {
        ExecListIter::new(
            &mut unsafe { self.nir.as_mut() }.variables,
            offset_of!(nir_variable, node),
        )
    }

    pub fn num_images(&self) -> u8 {
        unsafe { (*self.nir.as_ptr()).info.num_images }
    }

    pub fn num_textures(&self) -> u8 {
        unsafe { (*self.nir.as_ptr()).info.num_textures }
    }

    pub fn reset_scratch_size(&self) {
        unsafe {
            (*self.nir.as_ptr()).scratch_size = 0;
        }
    }

    pub fn scratch_size(&self) -> u32 {
        unsafe { (*self.nir.as_ptr()).scratch_size }
    }

    pub fn shared_size(&self) -> u32 {
        unsafe { (*self.nir.as_ptr()).info.shared_size }
    }

    pub fn workgroup_size(&self) -> [u16; 3] {
        unsafe { (*self.nir.as_ptr()).info.workgroup_size }
    }

    pub fn subgroup_size(&self) -> u8 {
        let subgroup_size = unsafe { (*self.nir.as_ptr()).info.subgroup_size };
        let valid_subgroup_sizes = [
            gl_subgroup_size::SUBGROUP_SIZE_REQUIRE_8,
            gl_subgroup_size::SUBGROUP_SIZE_REQUIRE_16,
            gl_subgroup_size::SUBGROUP_SIZE_REQUIRE_32,
            gl_subgroup_size::SUBGROUP_SIZE_REQUIRE_64,
            gl_subgroup_size::SUBGROUP_SIZE_REQUIRE_128,
        ];

        if valid_subgroup_sizes.contains(&subgroup_size) {
            subgroup_size as u8
        } else {
            0
        }
    }

    pub fn num_subgroups(&self) -> u8 {
        unsafe { (*self.nir.as_ptr()).info.num_subgroups }
    }

    pub fn set_workgroup_size_variable_if_zero(&self) {
        let nir = self.nir.as_ptr();
        unsafe {
            (*nir)
                .info
                .set_workgroup_size_variable((*nir).info.workgroup_size[0] == 0);
        }
    }

    pub fn set_has_variable_shared_mem(&mut self, val: bool) {
        unsafe {
            self.nir
                .as_mut()
                .info
                .anon_1
                .cs
                .set_has_variable_shared_mem(val)
        }
    }

    pub fn variables_with_mode(
        &mut self,
        mode: nir_variable_mode,
    ) -> impl Iterator<Item = &mut nir_variable> {
        self.variables()
            .filter(move |v| v.data.mode() & mode.0 != 0)
    }

    pub fn extract_constant_initializers(&self) {
        let nir = self.nir.as_ptr();
        unsafe {
            if (*nir).constant_data_size > 0 {
                assert!((*nir).constant_data.is_null());
                (*nir).constant_data = rzalloc_size(nir.cast(), (*nir).constant_data_size as usize);
                nir_gather_explicit_io_initializers(
                    nir,
                    (*nir).constant_data,
                    (*nir).constant_data_size as usize,
                    nir_variable_mode::nir_var_mem_constant,
                );
            }
        }
    }

    pub fn has_constant(&self) -> bool {
        unsafe {
            !self.nir.as_ref().constant_data.is_null() && self.nir.as_ref().constant_data_size > 0
        }
    }

    pub fn has_printf(&self) -> bool {
        unsafe {
            !self.nir.as_ref().printf_info.is_null() && self.nir.as_ref().printf_info_count != 0
        }
    }

    pub fn take_printf_info(&mut self) -> Option<NirPrintfInfo> {
        let nir = unsafe { self.nir.as_mut() };

        let info = nir.printf_info;
        if info.is_null() {
            return None;
        }
        let count = nir.printf_info_count as usize;

        unsafe {
            ralloc_steal(ptr::null(), info.cast());

            for i in 0..count {
                ralloc_steal(info.cast(), (*info.add(i)).arg_sizes.cast());
                ralloc_steal(info.cast(), (*info.add(i)).strings.cast());
            }
        };

        let result = Some(NirPrintfInfo {
            count: count,
            printf_info: info,
        });

        nir.printf_info_count = 0;
        nir.printf_info = ptr::null_mut();

        result
    }

    pub fn get_constant_buffer(&self) -> &[u8] {
        unsafe {
            let nir = self.nir.as_ref();
            slice::from_raw_parts(nir.constant_data.cast(), nir.constant_data_size as usize)
        }
    }

    pub fn preserve_fp16_denorms(&mut self) {
        unsafe {
            self.nir.as_mut().info.float_controls_execution_mode |=
                float_controls::FLOAT_CONTROLS_DENORM_PRESERVE_FP16 as u16;
        }
    }

    pub fn reads_sysval(&self, sysval: gl_system_value) -> bool {
        let nir = unsafe { self.nir.as_ref() };
        bitset::test_bit(&nir.info.system_values_read, sysval as u32)
    }

    pub fn add_var(
        &self,
        mode: nir_variable_mode,
        glsl_type: *const glsl_type,
        loc: usize,
        name: &str,
    ) -> *mut nir_variable {
        let name = CString::new(name).unwrap();
        unsafe {
            let var = nir_variable_create(self.nir.as_ptr(), mode, glsl_type, name.as_ptr());
            (*var).data.location = loc.try_into().unwrap();
            var
        }
    }
}

impl Drop for NirShader {
    fn drop(&mut self) {
        unsafe { ralloc_free(self.nir.as_ptr().cast()) };
    }
}
