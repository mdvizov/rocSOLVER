/* ************************************************************************
 * Copyright 2020 Advanced Micro Devices, Inc.
 * ************************************************************************ */

#include "norm.hpp"
#include "rocsolver_test.hpp"
#include "rocsolver_arguments.hpp"
#include "rocsolver.hpp"
#include "cblas_interface.h"
#include "clientcommon.hpp"

template <typename T>
void larfg_checkBadArgs(const rocblas_handle handle, 
                         const rocblas_int n, 
                         T da,
                         T dx, 
                         const rocblas_int inc,
                         T dt)
{
    // handle
    EXPECT_ROCBLAS_STATUS(rocsolver_larfg(nullptr,n,da,dx,inc,dt),
                          rocblas_status_invalid_handle); 

    // values
    // N/A
    
    // pointers
    EXPECT_ROCBLAS_STATUS(rocsolver_larfg(handle,n,(T)nullptr,dx,inc,dt),
                          rocblas_status_invalid_pointer);
    EXPECT_ROCBLAS_STATUS(rocsolver_larfg(handle,n,da,(T)nullptr,inc,dt),
                          rocblas_status_invalid_pointer);
    EXPECT_ROCBLAS_STATUS(rocsolver_larfg(handle,n,da,dx,inc,(T)nullptr),
                          rocblas_status_invalid_pointer);

    // quick return with invalid pointers
    EXPECT_ROCBLAS_STATUS(rocsolver_larfg(handle,0,(T)nullptr,(T)nullptr,inc,(T)nullptr),
                          rocblas_status_success);
}

template <typename T>
void testing_larfg_bad_arg()
{
    // safe arguments
    rocblas_local_handle handle;  
    rocblas_int n = 2;
    rocblas_int inc = 1;

    // memory allocation
    device_strided_batch_vector<T> da(1,1,1,1);
    device_strided_batch_vector<T> dx(1,1,1,1);
    device_strided_batch_vector<T> dt(1,1,1,1);
    CHECK_HIP_ERROR(da.memcheck());
    CHECK_HIP_ERROR(dx.memcheck());
    CHECK_HIP_ERROR(dt.memcheck());

    // check bad arguments
    larfg_checkBadArgs(handle,n,da.data(),dx.data(),inc,dt.data());
}   


template <typename T, typename Td, typename Th> 
void larfg_getError(const rocblas_handle handle,
                         const rocblas_int n,
                         Td &da,
                         Td &dx,
                         const rocblas_int inc,
                         Td &dt,
                         Th &ha,
                         Th &hx,
                         Th &hxr,
                         Th &ht,
                         double *max_err)
{
    //initialize data 
    rocblas_init<T>(ha, true);
    rocblas_init<T>(hx, true);

    // copy data from CPU to device
    CHECK_HIP_ERROR(da.transfer_from(ha));
    CHECK_HIP_ERROR(dx.transfer_from(hx));

    // execute computations
    //GPU lapack
    CHECK_ROCBLAS_ERROR(rocsolver_larfg(handle,n,da.data(),dx.data(),inc,dt.data()));
    CHECK_HIP_ERROR(hxr.transfer_from(dx));

    //CPU lapack
    cblas_larfg<T>(n,ha[0],hx[0],inc,ht[0]);

    // error is ||hx - hxr|| (not necessary to check tau, for now)
    // (THIS DOES NOT ACCOUNT FOR NUMERICAL REPRODUCIBILITY ISSUES. 
    // IT MIGHT BE REVISITED IN THE FUTURE)
    // using norm-1 which is infinity norm for this data setup 
    *max_err = norm_error('O',1,n-1,inc,hx[0],hxr[0]);
}


template <typename T, typename Td, typename Th> 
void larfg_getPerfData(const rocblas_handle handle,
                         const rocblas_int n,
                         Td &da,
                         Td &dx,
                         const rocblas_int inc,
                         Td &dt,
                         Th &ha,
                         Th &hx,
                         Th &ht,
                         double *gpu_time_used,
                         double *cpu_time_used,
                         const rocblas_int hot_calls)
{
    // cpu-lapack performance
    *cpu_time_used = get_time_us();
    cblas_larfg<T>(n,ha[0],hx[0],inc,ht[0]);
    *cpu_time_used = get_time_us() - *cpu_time_used;
        
    // cold calls    
    for(int iter = 0; iter < 2; iter++)
        CHECK_ROCBLAS_ERROR(rocsolver_larfg(handle,n,da.data(),dx.data(),inc,dt.data()));

    // gpu-lapack performance
    *gpu_time_used = get_time_us();
    for(int iter = 0; iter < hot_calls; iter++)
        rocsolver_larfg(handle,n,da.data(),dx.data(),inc,dt.data());
    *gpu_time_used = (get_time_us() - *gpu_time_used) / hot_calls;       
}


template <typename T> 
void testing_larfg(Arguments argus) 
{
    // get arguments 
    rocblas_local_handle handle;  
    rocblas_int n = argus.N;
    rocblas_int inc = argus.incx;
    rocblas_int hot_calls = argus.iters;
    
    // check non-supported values 
    // N/A

    // determine sizes
    size_t size_x = size_t(n-1);
    size_t stx = size_x * inc;
    double max_error = 0, gpu_time_used = 0, cpu_time_used = 0;

    size_t size_xr = argus.unit_check || argus.norm_check ? size_x : 0;
    size_t stxr = argus.unit_check || argus.norm_check ? stx : 0;

    // check invalid sizes
    bool invalid_size = (n < 0 || inc < 1);
    if (invalid_size) {
        EXPECT_ROCBLAS_STATUS(rocsolver_larfg(handle,n,(T*)nullptr,(T*)nullptr,inc,(T*)nullptr),
                              rocblas_status_invalid_size);

        if (argus.timing)  
            ROCSOLVER_BENCH_INFORM(1);

        return;
    }             

    // memory allocations
    host_strided_batch_vector<T> hx(size_x,inc,stx,1);
    host_strided_batch_vector<T> hxr(size_xr,inc,stxr,1);
    host_strided_batch_vector<T> ha(1,1,1,1);
    host_strided_batch_vector<T> ht(1,1,1,1);
    device_strided_batch_vector<T> dx(size_x,inc,stx,1);
    device_strided_batch_vector<T> da(1,1,1,1);
    device_strided_batch_vector<T> dt(1,1,1,1);
    CHECK_HIP_ERROR(da.memcheck());
    if (size_x) CHECK_HIP_ERROR(dx.memcheck());
    CHECK_HIP_ERROR(dt.memcheck());
    
    // check quick return
    if (n == 0) {
        EXPECT_ROCBLAS_STATUS(rocsolver_larfg(handle,n,da.data(),dx.data(),inc,dt.data()),
                              rocblas_status_success);

        if (argus.timing)  
            ROCSOLVER_BENCH_INFORM(0);
        
        return;
    }

    // check computations
    if (argus.unit_check || argus.norm_check)
        larfg_getError<T>(handle, n, da, dx, inc, dt, 
                          ha, hx, hxr, ht, &max_error); 

    // collect performance data 
    if (argus.timing) 
        larfg_getPerfData<T>(handle, n, da, dx, inc, dt,
                          ha, hx, ht, &gpu_time_used, &cpu_time_used, hot_calls); 
        
    // validate results for rocsolver-test
    // using n * machine_precision as tolerance
    if (argus.unit_check) 
        rocsolver_test_check<T>(max_error,n);     

    // output results for rocsolver-bench
    if (argus.timing) {
        rocblas_cout << "\n============================================\n";
        rocblas_cout << "Arguments:\n";
        rocblas_cout << "============================================\n";
        rocsolver_bench_output("n", "inc");
        rocsolver_bench_output(n, inc);

        rocblas_cout << "\n============================================\n";
        rocblas_cout << "Results:\n";
        rocblas_cout << "============================================\n";
        if (argus.norm_check) {
            rocsolver_bench_output("cpu_time", "gpu_time", "error");
            rocsolver_bench_output(cpu_time_used, gpu_time_used, max_error);
        }
        else {
            rocsolver_bench_output("cpu_time", "gpu_time");
            rocsolver_bench_output(cpu_time_used, gpu_time_used);
        }
        rocblas_cout << std::endl;
    }
}

#undef ERROR_EPS_MULTIPLIER