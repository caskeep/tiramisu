#include "wrapper_heat2d_dist.h"

#include "Halide.h"
#include "halide_image_io.h"
#include "tiramisu/utils.h"
#include <cstdlib>
#include <iostream>
#include <mpi.h>

#define REQ MPI_THREAD_FUNNELED

// TODO (Jess): define how to map ranks to either CPU or GPU (which should go where)
#include <vector>
CPU_RANK_RANGES
GPU_RANK_RANGES
int main(int, char**)
{

    int provided = -1;
    MPI_Init_thread(NULL, NULL, REQ, &provided);
    assert(provided == REQ && "Did not get the appropriate MPI thread requirement.");
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (NUM_CPU_RANKS != 0) {
      assert(((N-2) * CPU_SPLIT) % 10 == 0);
      assert((((N-2) * CPU_SPLIT) / 10) % NUM_CPU_RANKS == 0);
    }
    if (NUM_GPU_RANKS != 0) {
      assert(((N-2) * GPU_SPLIT) % 10 == 0);
      assert((((N-2) * GPU_SPLIT) / 10) % NUM_GPU_RANKS == 0);
    }
    std::vector<std::chrono::duration<double,std::milli>> duration_vector_1;
    std::vector<std::chrono::duration<double,std::milli>> duration_vector_2;

    // figure out how many rows should be associated with this rank
    int rank_N;
    if (std::find(cpu_rank_ranges.begin(), cpu_rank_ranges.begin(), rank) != cpu_rank_ranges.end()) {
      rank_N = (((N-2) * CPU_SPLIT) / 10) / NUM_CPU_RANKS;
      std::cerr << "Rank " << rank << " is on the CPU and has " << rank_N << " out of " << N << std::endl;
    } else if (std::find(gpu_rank_ranges.begin(), gpu_rank_ranges.begin(), rank) != gpu_rank_ranges.end()) {
      rank_N = (((N-2) * GPU_SPLIT) / 10) / NUM_GPU_RANKS;
      std::cerr << "Rank " << rank << " is on the GPU and has " << rank_N << " out of " << N << std::endl;
    } else {
      assert(false && "Rank not found.");
    }
    
    Halide::Buffer<float> input(Halide::Float(32), N, rank_N+2);
    Halide::Buffer<float> full_input(Halide::Float(32), N, rank_N+2); // contains borders and stuff
    // Init randomly
    for (int i = 0; i < rank_N; i++) { // 
      for (int j = 0; j < N; j++) {
	((float*)(input.raw_buffer()->host))[(i+1)*N+j] = 1.0f;
      }
    }
    for (int i = 0; i < rank_N; i++) { // this isn't modified after this point
      for (int j = 0; j < N; j++) {
	((float*)(full_input.raw_buffer()->host))[(i+1)*N+j] = 1.0f;
      }
    }
    //    for (int y = 0; y < input.height(); ++y) {
    //        for (int x = 0; x < input.width()-2; ++x) {
    //            input(x, y) = 1.0f;
    //        }
    //    }

    Halide::Buffer<float> output1(N/*input.width()*/, rank_N);//input.height());
    Halide::Buffer<float> output2(input.width(), input.height());

    // Warm up code.
    heat2d_dist_tiramisu(input.raw_buffer(), output1.raw_buffer());
    //    heat2d_dist_ref(input.raw_buffer(), output2.raw_buffer());
    // Tiramisu
    MPI_Barrier(MPI_COMM_WORLD);
    for (int i=0; i<N_TESTS; i++)
    {
        auto start1 = std::chrono::high_resolution_clock::now();
	MPI_Barrier(MPI_COMM_WORLD);
	heat2d_dist_tiramisu(input.raw_buffer(), output1.raw_buffer());
	MPI_Barrier(MPI_COMM_WORLD);
        auto end1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double,std::milli> duration1 = end1 - start1;
        duration_vector_1.push_back(duration1);
    }

    // Reference
    for (int i= 0; i<N_TESTS; i++)
    {
        MPI_Barrier(MPI_COMM_WORLD);
        auto start2 = std::chrono::high_resolution_clock::now();
	//        heat2d_ref_dist(input.raw_buffer(), output2.raw_buffer());
        auto end2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double,std::milli> duration2 = end2 - start2;
        duration_vector_2.push_back(duration2);
	MPI_Barrier(MPI_COMM_WORLD);
    }
    if (rank == 0) {
      print_time("performance_CPU.csv", "heat2d",
		 {"Tiramisu", "Halide"},
		 {median(duration_vector_1), median(duration_vector_2)});
      std::cerr << std::endl;
    }

    // Have to fix the boundary condition cause we do extra in the tiramisu version
    /*    if (rank == (NUM_CPU_RANKS+NUM_GPU_RANKS-1)) {
      for (int j = 0; j < N; j++) {
	output1(j, N-1) = 0.0f;
      }
      for (int j = 0; j < N; j++) {
	output1(N-1,j) = 0.0f;
      }
      }*/
    //    if (CHECK_CORRECTNESS)
    //      compare_buffers_approximately("benchmark_heat2d", output1, output2);
    if (CHECK_CORRECTNESS) {
      for (int i = 1; i < rank_N; i++) {
	for (int j = 1; j < N-1; j++) {	  
	  float expected = full_input(j,i) * 0.3 + (input(j,i-1)+input(j,i+1)+input(j-1,i)+input(j+1,i))*0.4;
	  float got = output1(j,i);
	  if (std::fabs(expected - got) > 0.01f) {
	    std::cerr << "Rank " << rank << " expected " << expected << " but got " << got << " at (" << i << ", " << j << ")" << std::endl;
	    assert(false);
	  }
	}
      }
    }


    MPI_Finalize();
    
    return 0;
}