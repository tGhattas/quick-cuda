#include <cstdio>
#include <cuda.h>
#include <string>

#include "graph.h"
#include "bfsCPU.h"

void runCpu(int startVertex, Graph &G, std::vector<int> &distance,
            std::vector<int> &parent, std::vector<bool> &visited) {
    printf("Starting sequential bfs.\n");
    auto start = std::chrono::steady_clock::now();
    bfsCPU(startVertex, G, distance, parent, visited);
    auto end = std::chrono::steady_clock::now();
    long duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    printf("Elapsed time in milliseconds : %li ms.\n\n", duration);
}

void checkError(CUresult error, std::string msg) {
    if (error != CUDA_SUCCESS) {
        printf("%s: %d\n", msg.c_str(), error);
        exit(1);
    }
}

CUdevice cuDevice;
CUcontext cuContext;
CUmodule cuModule;

CUfunction cuSimpleBfs;

CUdeviceptr d_adjacencyList;
CUdeviceptr d_edgesOffset;
CUdeviceptr d_edgesSize;
CUdeviceptr d_distance;
CUdeviceptr d_parent;


int *incrDegrees;

void initCuda(Graph &G) {
    //initialize CUDA
    cuInit(0);
    checkError(cuDeviceGet(&cuDevice, 0), "cannot get device 0");
    checkError(cuCtxCreate(&cuContext, 0, cuDevice), "cannot create context");
    checkError(cuModuleLoad(&cuModule, "quickCUDA.ptx"), "cannot load module");
    checkError(cuModuleGetFunction(&cuSimpleBfs, cuModule, "simpleBfs"), "cannot get kernel handle");

    //copy memory to device
    checkError(cuMemAlloc(&d_adjacencyList, G.numEdges * sizeof(int)), "cannot allocate d_adjacencyList");
    checkError(cuMemAlloc(&d_edgesOffset, G.numVertices * sizeof(int)), "cannot allocate d_edgesOffset");
    checkError(cuMemAlloc(&d_edgesSize, G.numVertices * sizeof(int)), "cannot allocate d_edgesSize");
    checkError(cuMemAlloc(&d_distance, G.numVertices * sizeof(int)), "cannot allocate d_distance");
    checkError(cuMemAlloc(&d_parent, G.numVertices * sizeof(int)), "cannot allocate d_parent");

    checkError(cuMemcpyHtoD(d_adjacencyList, G.adjacencyList.data(), G.numEdges * sizeof(int)),
               "cannot copy to d_adjacencyList");
    checkError(cuMemcpyHtoD(d_edgesOffset, G.edgesOffset.data(), G.numVertices * sizeof(int)),
               "cannot copy to d_edgesOffset");
    checkError(cuMemcpyHtoD(d_edgesSize, G.edgesSize.data(), G.numVertices * sizeof(int)),
               "cannot copy to d_edgesSize");


}

void finalizeCuda() {
    //free memory
    checkError(cuMemFree(d_adjacencyList), "cannot free memory for d_adjacencyList");
    checkError(cuMemFree(d_edgesOffset), "cannot free memory for d_edgesOffset");
    checkError(cuMemFree(d_edgesSize), "cannot free memory for d_edgesSize");
    checkError(cuMemFree(d_distance), "cannot free memory for d_distance");
    checkError(cuMemFree(d_parent), "cannot free memory for d_parent");
}

void checkOutput(std::vector<int> &distance, std::vector<int> &expectedDistance, Graph &G) {
    for (int i = 0; i < G.numVertices; i++) {
        if (distance[i] != expectedDistance[i]) {
            printf("%d %d %d\n", i, distance[i], expectedDistance[i]);
            printf("Wrong output!\n");
            exit(1);
        }
    }

    printf("Output OK!\n\n");
}

void initializeCudaBfs(int startVertex, std::vector<int> &distance, std::vector<int> &parent, Graph &G) {
    //initialize values
    std::fill(distance.begin(), distance.end(), std::numeric_limits<int>::max());
    std::fill(parent.begin(), parent.end(), std::numeric_limits<int>::max());
    distance[startVertex] = 0;
    parent[startVertex] = 0;

    checkError(cuMemcpyHtoD(d_distance, distance.data(), G.numVertices * sizeof(int)),
               "cannot copy to d)distance");
    checkError(cuMemcpyHtoD(d_parent, parent.data(), G.numVertices * sizeof(int)),
               "cannot copy to d_parent");

}

void finalizeCudaBfs(std::vector<int> &distance, std::vector<int> &parent, Graph &G) {
    //copy memory from device
    checkError(cuMemcpyDtoH(distance.data(), d_distance, G.numVertices * sizeof(int)),
               "cannot copy d_distance to host");
    checkError(cuMemcpyDtoH(parent.data(), d_parent, G.numVertices * sizeof(int)), "cannot copy d_parent to host");

}

void runCudaSimpleBfs(int startVertex, Graph &G, std::vector<int> &distance,
                      std::vector<int> &parent) {
    initializeCudaBfs(startVertex, distance, parent, G);

    int *changed;
    checkError(cuMemAllocHost((void **) &changed, sizeof(int)), "cannot allocate changed");

    //launch kernel
    printf("Starting simple parallel bfs.\n");
    auto start = std::chrono::steady_clock::now();

    *changed = 1;
    int level = 0;
    while (*changed) {
        *changed = 0;
        void *args[] = {&G.numVertices, &level, &d_adjacencyList, &d_edgesOffset, &d_edgesSize, &d_distance, &d_parent,
                        &changed};
        checkError(cuLaunchKernel(cuSimpleBfs, G.numVertices / 1024 + 1, 1, 1,
                                  1024, 1, 1, 0, 0, args, 0),
                   "cannot run kernel simpleBfs");
        cuCtxSynchronize();
        level++;
    }


    auto end = std::chrono::steady_clock::now();
    long duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    printf("Elapsed time in milliseconds : %li ms.\n", duration);

    finalizeCudaBfs(distance, parent, G);
}



int main(int argc, char **argv) {

    // read graph from standard input
    Graph G;
    int startVertex = atoi(argv[1]);
    readGraph(G, argc, argv);

    printf("Number of vertices %d\n", G.numVertices);
    printf("Number of edges %d\n\n", G.numEdges);

    //vectors for results
    std::vector<int> distance(G.numVertices, std::numeric_limits<int>::max());
    std::vector<int> parent(G.numVertices, std::numeric_limits<int>::max());
    std::vector<bool> visited(G.numVertices, false);

    //run CPU sequential bfs
    runCpu(startVertex, G, distance, parent, visited);

    //save results from sequential bfs
    std::vector<int> expectedDistance(distance);
    std::vector<int> expectedParent(parent);

    initCuda(G);
    //run CUDA simple parallel bfs
    runCudaSimpleBfs(startVertex, G, distance, parent);
    checkOutput(distance, expectedDistance, G);

    finalizeCuda();
    return 0;
}