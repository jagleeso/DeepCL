// Copyright Hugh Perkins 2015 hughperkins at gmail
//
// This Source Code Form is subject to the terms of the Mozilla Public License, 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.

#include <algorithm>

#include "PropagateFc.h"
#include "stringhelper.h"
#include "StatefulTimer.h"

using namespace std;

#undef VIRTUAL
#undef STATIC
#define VIRTUAL
#define STATIC

VIRTUAL PropagateFc::~PropagateFc() {
    delete kernel1;
    delete kernel_reduce;
    delete kernel_activate;
    delete kPerElementTiledAdd;
}
VIRTUAL void PropagateFc::propagate( int batchSize, CLWrapper *dataWrapper, CLWrapper *weightsWrapper, CLWrapper *biasWeightsWrapper, CLWrapper *resultsWrapper ) {
    StatefulTimer::timeCheck("PropagateFc::propagate begin");

    const int maxWorkgroupSize = cl->getMaxWorkgroupSize();

    const int results1Size = batchSize * dim.numFilters * dim.numInputPlanes * dim.filterSize;
    float *results1 = new float[ results1Size ];
    CLWrapper *results1Wrapper = cl->wrap( results1Size, results1 );
    results1Wrapper->createOnDevice();

    const int results2Size = batchSize * dim.numFilters * dim.numInputPlanes;
    float *results2 = new float[ results2Size ];
    CLWrapper *results2Wrapper = cl->wrap( results2Size, results2 );
    results2Wrapper->createOnDevice();

    kernel1->in(batchSize);
    kernel1->input( dataWrapper );
    kernel1->input( weightsWrapper);
//    if( dim.biased ) kernel1->input( biasWeightsWrapper );
    kernel1->output( results1Wrapper );
    kernel1->localFloats( dim.inputBoardSize );
    kernel1->localFloats( dim.numFilters * dim.filterSize );

    int workgroupSize = dim.numFilters;
    int numWorkgroups = dim.filterSize * dim.numInputPlanes;

    int globalSize = workgroupSize * numWorkgroups;
    kernel1->run_1d( globalSize, workgroupSize );
    cl->finish();
    StatefulTimer::timeCheck("PropagateFc::propagate after first kernel");

    // now reduce over rows 
    kernel_reduce->in(batchSize * dim.numFilters * dim.numInputPlanes)
        ->in( dim.filterSize )
        ->in( results1Wrapper )->out( results2Wrapper );
    int maxglobalId = batchSize * dim.numFilters * dim.numInputPlanes;
//    numWorkgroups = ( maxglobalId + maxWorkgroupSize - 1 ) / maxWorkgroupSize;
//    kernel_reduce->run_1d( numWorkgroups * maxWorkgroupSize, maxWorkgroupSize );
    numWorkgroups = ( maxglobalId + 64 - 1 ) / 64;
    kernel_reduce->run_1d( numWorkgroups * 64, 64 );
    cl->finish();
    StatefulTimer::timeCheck("PropagateFc::propagate after reduce1");

    // reduce over input planes 
    kernel_reduce->in(batchSize * dim.numFilters)->in( dim.numInputPlanes )
        ->in( results2Wrapper )->out( resultsWrapper );
    maxglobalId = batchSize * dim.numFilters;
    numWorkgroups = ( batchSize * dim.numFilters + maxWorkgroupSize - 1 ) / maxWorkgroupSize;
    kernel_reduce->run_1d( numWorkgroups * maxWorkgroupSize, maxWorkgroupSize );
//    numWorkgroups = ( maxglobalId + 64 - 1 ) / 64;
//    kernel_reduce->run_1d( numWorkgroups * 64, 64 );
    cl->finish();
    StatefulTimer::timeCheck("PropagateFc::propagate after reduce2");

    // add bias...
    if( dim.biased ) {
        kPerElementTiledAdd->in( batchSize * dim.numFilters )->in( dim.numFilters )->inout( resultsWrapper )->in( biasWeightsWrapper );
        maxglobalId = batchSize * dim.numFilters;
        numWorkgroups = ( batchSize * dim.numFilters + maxWorkgroupSize - 1 ) / maxWorkgroupSize;
        kPerElementTiledAdd->run_1d( numWorkgroups * maxWorkgroupSize, maxWorkgroupSize );
        cl->finish();
        StatefulTimer::timeCheck("PropagateFc::propagate after add bias");        
    }

    kernel_activate->in( batchSize * dim.numFilters )
        ->inout( resultsWrapper );
    maxglobalId = batchSize * dim.numFilters;
    numWorkgroups = ( batchSize * dim.numFilters + maxWorkgroupSize - 1 ) / maxWorkgroupSize;
    kernel_activate->run_1d( numWorkgroups * maxWorkgroupSize, maxWorkgroupSize );
    cl->finish();
    StatefulTimer::timeCheck("PropagateFc::propagate after activate");

    delete results2Wrapper;
    delete[] results2;

    delete results1Wrapper;
    delete[] results1;
    StatefulTimer::timeCheck("PropagateFc::propagate end");
}
PropagateFc::PropagateFc( OpenCLHelper *cl, LayerDimensions dim, ActivationFunction const*fn ) :
        Propagate( cl, dim, fn )
            {

    if( dim.inputBoardSize != dim.filterSize ) {
        throw runtime_error("For PropagateFc, filtersize and inputboardsize must be identical");
    }
    if( dim.padZeros ) {
        throw runtime_error("For PropagateFc, padzeros must be disabled");
    }

    std::string options = "-D " + fn->getDefineName();
    options += dim.buildOptionsString();

    // [[[cog
    // import stringify
    // stringify.write_kernel2( "kernel1", "cl/propagate_fc.cl", "propagate_fc_workgroup_perrow", 'options' )
    // stringify.write_kernel2( "kernel_reduce", "cl/reduce_segments.cl", "reduce_segments", 'options' )
    // stringify.write_kernel2( "kernel_activate", "cl/activate.cl", "activate", 'options' )
    // # stringify.write_kernel2( "kPerElementAdd", "cl/per_element_add.cl", "per_element_add", 'options' )
    // stringify.write_kernel2( "kPerElementTiledAdd", "cl/per_element_add.cl", "per_element_tiled_add", 'options' )
    // ]]]
    // generated using cog:
    const char * kernel1Source =  
    "// Copyright Hugh Perkins 2014, 2015 hughperkins at gmail\n" 
    "//\n" 
    "// This Source Code Form is subject to the terms of the Mozilla Public License,\n" 
    "// v. 2.0. If a copy of the MPL was not distributed with this file, You can\n" 
    "// obtain one at http://mozilla.org/MPL/2.0/.\n" 
    "\n" 
    "// expected defines:\n" 
    "// one of: [ TANH | RELU | LINEAR ]\n" 
    "// BIASED (or not)\n" 
    "\n" 
    "#ifdef TANH\n" 
    "    #define ACTIVATION_FUNCTION(output) (tanh(output))\n" 
    "#elif defined SCALEDTANH\n" 
    "    #define ACTIVATION_FUNCTION(output) ( 1.7159f * tanh( 0.66667f * output))\n" 
    "#elif SIGMOID\n" 
    "    #define ACTIVATION_FUNCTION(output) (1.0f / (1 + exp(-output)))\n" 
    "#elif defined RELU\n" 
    "    #define ACTIVATION_FUNCTION(output) (output> 0 ? output : 0)\n" 
    "#elif defined LINEAR\n" 
    "    #define ACTIVATION_FUNCTION(output) (output)\n" 
    "#endif\n" 
    "\n" 
    "//#ifdef gOutputBoardSize // for previous tests that dont define it\n" 
    "// concept:\n" 
    "//  we want to share each input example across multiple filters\n" 
    "//   but an entire filter plane is 19*19*4 = 1.4KB\n" 
    "//   so eg 500 filter planes is 500* 1.4KB = 700KB, much larger than local storage\n" 
    "//   of ~43KB\n" 
    "//  - we could take eg 16 filters at a time, store one filter plane from each in local storage,\n" 
    "//  and then bring down one example plane at a time, into local storage, during iteration over n\n" 
    "//  - here though, we are going to store one row from one plane from each filter,\n" 
    "//  and process against one row, from same plane, from each example\n" 
    "//  so each workgroup will have one thread per filterId, eg 351 threads\n" 
    "//    each thread will add up over its assigned row\n" 
    "//  then, later we need to reduce over the rows\n" 
    "//   ... and also over the input planes?\n" 
    "//\n" 
    "// workgroupid [inputplane][filterrow]\n" 
    "// localid: [filterId]\n" 
    "//  each thread iterates over: [n][filtercol]\n" 
    "//  each thread is assigned to: one row, of one filter\n" 
    "//  workgroup is assigned to: same row, from each input plane\n" 
    "// local memory: one row from each output, = 128 * 19 * 4 = 9.8KB\n" 
    "//             1 * input row = \"0.076KB\"\n" 
    "// results1 structured as: [n][inputplane][filter][row], need to reduce again after\n" 
    "// this kernel assumes:\n" 
    "//   padzeros == 0 (mandatory)\n" 
    "//   filtersize == inputboardsize (mandatory)\n" 
    "//   inputboardsize == 19\n" 
    "//   filtersize == 19\n" 
    "//   outputBoardSize == 1\n" 
    "//   lots of outplanes/filters, hundreds, but less than max work groupsize, eg 350, 500, 361\n" 
    "//   lots of inplanes, eg 32-128\n" 
    "//   inputboardsize around 19, not too small\n" 
    "#if (gFilterSize == gInputBoardSize) && (gPadZeros == 0)\n" 
    "void kernel propagate_fc_workgroup_perrow( const int batchSize,\n" 
    "    global const float *images, global const float *filters,\n" 
    "//        #ifdef BIASED\n" 
    "//            global const float*biases,\n" 
    "//        #endif\n" 
    "    global float *results1,\n" 
    "    local float *_imageRow, local float *_filterRows ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "\n" 
    "    const int workgroupId = get_group_id(0);\n" 
    "    const int workgroupSize = get_local_size(0);\n" 
    "    const int localId = get_local_id(0);\n" 
    "\n" 
    "    const int inputPlaneId = workgroupId / gFilterSize;\n" 
    "    const int filterRowId = workgroupId % gFilterSize;\n" 
    "\n" 
    "    const int filterId = localId;\n" 
    "\n" 
    "    // first copy down filter row, which is per-thread, so we have to copy it all ourselves...\n" 
    "    global const float *filterRow = filters\n" 
    "        + filterId * gNumInputPlanes * gFilterSizeSquared\n" 
    "        + inputPlaneId * gFilterSizeSquared\n" 
    "        + filterRowId * gFilterSize;\n" 
    "//    filterRow = filters + localId * gNumInputPlanes * gFilterSizeSquared;\n" 
    "    local float *_threadFilterRow = _filterRows + localId * gFilterSize;\n" 
    "//    filterRow = filters;\n" 
    "    for( int i = 0; i < gFilterSize; i++ ) {\n" 
    "        _threadFilterRow[i] = filterRow[i];\n" 
    "    }\n" 
    "//    #ifdef BIASED\n" 
    "//    const float bias = biases[filterId];\n" 
    "//    #endif\n" 
    "    const int loopsPerExample = ( gInputBoardSize + workgroupSize - 1 ) / workgroupSize;\n" 
    "    // now loop over examples...\n" 
    "    for( int n = 0; n < batchSize; n++ ) {\n" 
    "        // copy down example row, which is global to all threads in workgroup\n" 
    "        // hopefully should be enough threads....\n" 
    "        // but we should check anyway really, since depends on number of filters configured,\n" 
    "        // not on relative size of filter and input board\n" 
    "        global const float *exampleRow = images\n" 
    "            + n * gNumInputPlanes * gInputBoardSizeSquared\n" 
    "            + inputPlaneId * gInputBoardSizeSquared\n" 
    "            + filterRowId * gInputBoardSize;\n" 
    "        barrier(CLK_LOCAL_MEM_FENCE);\n" 
    "        for( int loop = 0; loop < loopsPerExample; loop++ ) {\n" 
    "            int offset = loop * workgroupSize + localId;\n" 
    "            if( offset < gInputBoardSize ) {\n" 
    "                _imageRow[offset] = exampleRow[offset];\n" 
    "            }\n" 
    "        }\n" 
    "        barrier(CLK_LOCAL_MEM_FENCE);\n" 
    "        // add up the values in our row...\n" 
    "        float sum = 0;\n" 
    "        for( int filterCol = 0; filterCol < gFilterSize; filterCol++ ) {\n" 
    "            sum += _imageRow[ filterCol ] * _threadFilterRow[ filterCol ];\n" 
    "        }\n" 
    "//        #ifdef BIASED\n" 
    "//        sum += bias;\n" 
    "//        #endif\n" 
    "        // note: dont activate yet, since need to reduce again\n" 
    "        // results structured as: [n][filter][inputplane][filterrow], need to reduce again after\n" 
    "        if( localId < gNumFilters ) {\n" 
    "            results1[ n * gNumInputPlanes * gNumFilters * gFilterSize\n" 
    "                + inputPlaneId * gFilterSize\n" 
    "                + filterId * gNumInputPlanes * gFilterSize + filterRowId ] = sum;\n" 
    "        }\n" 
    "    }\n" 
    "}\n" 
    "#endif\n" 
    "//#endif\n" 
    "\n" 
    "// each thread handles one filter, ie globalId as [n][inputplane][filterId]\n" 
    "// results1: [n][inputplane][filter][filterrow]\n" 
    "// results2: [n][inputplane][filter]\n" 
    "#ifdef ACTIVATION_FUNCTION // protect against not defined\n" 
    "kernel void reduce_rows( const int batchSize, global float const *results1, global float*results2 ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "    const int n = globalId / gNumInputPlanes / gNumFilters;\n" 
    "    if( n >= batchSize ) {\n" 
    "        return;\n" 
    "    }\n" 
    "    const int filterId = globalId % gNumFilters;\n" 
    "    float sum = 0;\n" 
    "    global const float *results1Col = results1 + globalId * gFilterSize;\n" 
    "    for( int filterRow = 0; filterRow < gFilterSize; filterRow++ ) {\n" 
    "        sum += results1Col[filterRow];\n" 
    "    }\n" 
    "    results2[globalId] = sum;\n" 
    "}\n" 
    "#endif\n" 
    "\n" 
    "// each thread handles one filter, ie globalId as [n][filterId]\n" 
    "// results2: [n][inputplane][filter]\n" 
    "// results: [n][filter]\n" 
    "#ifdef ACTIVATION_FUNCTION // protect against not defined\n" 
    "kernel void reduce_inputplanes( const int batchSize, global float const *results2, global float*results ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "    const int n = globalId / gNumFilters;\n" 
    "    if( n >= batchSize ) {\n" 
    "        return;\n" 
    "    }\n" 
    "    const int filterId = globalId % gNumFilters;\n" 
    "    float sum = 0;\n" 
    "    global const float *results2Col = results2 + globalId * gNumInputPlanes;\n" 
    "    for( int inputPlane = 0; inputPlane < gNumInputPlanes; inputPlane++ ) {\n" 
    "        sum += results2Col[inputPlane];\n" 
    "    }\n" 
    "    // activate...\n" 
    "    results[globalId] = ACTIVATION_FUNCTION(sum);\n" 
    "}\n" 
    "#endif\n" 
    "\n" 
    "#ifdef gOutBoardSize // for previous tests that dont define it\n" 
    "#ifdef ACTIVATION_FUNCTION // protect against not defined\n" 
    "// workgroupid [n][outputplane]\n" 
    "// localid: [filterrow][filtercol]\n" 
    "//  each thread iterates over: [inplane]\n" 
    "// this kernel assumes:\n" 
    "//   padzeros == 0 (mandatory)\n" 
    "//   filtersize == inputboardsize (mandatory)\n" 
    "//   outputBoardSize == 1\n" 
    "//   lots of outplanes, hundreds, but less than max work groupsize, eg 350, 500, 361\n" 
    "//   lots of inplanes, eg 32\n" 
    "//   inputboardsize around 19, not too small\n" 
    "#if gFilterSize == gInputBoardSize && gPadZeros == 0\n" 
    "void kernel propagate_filter_matches_inboard( const int batchSize,\n" 
    "      global const float *images, global const float *filters,\n" 
    "        #ifdef BIASED\n" 
    "            global const float*biases,\n" 
    "        #endif\n" 
    "    global float *results,\n" 
    "    local float *_upstreamBoard, local float *_filterBoard ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "\n" 
    "    const int workgroupId = get_group_id(0);\n" 
    "    const int workgroupSize = get_local_size(0);\n" 
    "    const int n = workgroupId / gNumOutPlanes;\n" 
    "    const int outPlane = workgroupId % gNumOutPlanes;\n" 
    "\n" 
    "    const int localId = get_local_id(0);\n" 
    "    const int filterRow = localId / gFilterSize;\n" 
    "    const int filterCol = localId % gFilterSize;\n" 
    "\n" 
    "    float sum = 0;\n" 
    "    for( int upstreamPlane = 0; upstreamPlane < gUpstreamNumPlanes; upstreamPlane++ ) {\n" 
    "        int thisUpstreamBoardOffset = ( n * gUpstreamNumPlanes + upstreamPlane ) * gUpstreamBoardSizeSquared;\n" 
    "        barrier(CLK_LOCAL_MEM_FENCE);\n" 
    "        for( int i = 0; i < numUpstreamsPerThread; i++ ) {\n" 
    "            int thisOffset = workgroupSize * i + localId;\n" 
    "            if( thisOffset < gUpstreamBoardSizeSquared ) {\n" 
    "                _upstreamBoard[ thisOffset ] = images[ thisUpstreamBoardOffset + thisOffset ];\n" 
    "            }\n" 
    "        }\n" 
    "        const int filterGlobalOffset = ( outPlane * gUpstreamNumPlanes + upstreamPlane ) * gFilterSizeSquared;\n" 
    "        for( int i = 0; i < numFilterPixelsPerThread; i++ ) {\n" 
    "            int thisOffset = workgroupSize * i + localId;\n" 
    "            if( thisOffset < gFilterSizeSquared ) {\n" 
    "                _filterCube[thisOffset] = filters[filterGlobalOffset + thisOffset];\n" 
    "            }\n" 
    "        }\n" 
    "        barrier(CLK_LOCAL_MEM_FENCE);\n" 
    "        if( localId < gOutBoardSizeSquared ) {\n" 
    "            for( int u = minu; u <= maxu; u++ ) {\n" 
    "                int inputRow = outputRow + u + ( gPadZeros ? 0 : gHalfFilterSize );\n" 
    "                int inputboardrowoffset = inputRow * gUpstreamBoardSize;\n" 
    "                int filterrowoffset = (u+gHalfFilterSize) * gFilterSize + gHalfFilterSize;\n" 
    "                for( int v = minv; v <= maxv; v++ ) {\n" 
    "                    int inputCol = outputCol + v + ( gPadZeros ? 0 : gHalfFilterSize );\n" 
    "                    sum += _upstreamBoard[ inputboardrowoffset + inputCol] * _filterCube[ filterrowoffset + v ];\n" 
    "                }\n" 
    "            }\n" 
    "        }\n" 
    "    }\n" 
    "    #ifdef BIASED\n" 
    "        sum += biases[outPlane];\n" 
    "    #endif\n" 
    "    // results are organized like [imageid][filterid][row][col]\n" 
    "    int resultIndex = ( n * gNumOutPlanes + outPlane ) * gOutBoardSizeSquared + localId;\n" 
    "    if( localId < gOutBoardSizeSquared ) {\n" 
    "        results[resultIndex ] = ACTIVATION_FUNCTION(sum);\n" 
    "//        results[resultIndex ] = 123;\n" 
    "    }\n" 
    "}\n" 
    "#endif\n" 
    "#endif\n" 
    "#endif\n" 
    "\n" 
    "\n" 
    "";
    kernel1 = cl->buildKernelFromString( kernel1Source, "propagate_fc_workgroup_perrow", options, "cl/propagate_fc.cl" );
    // generated using cog:
    const char * kernel_reduceSource =  
    "// Copyright Hugh Perkins 2015 hughperkins at gmail\n" 
    "//\n" 
    "// This Source Code Form is subject to the terms of the Mozilla Public License,\n" 
    "// v. 2.0. If a copy of the MPL was not distributed with this file, You can\n" 
    "// obtain one at http://mozilla.org/MPL/2.0/.\n" 
    "\n" 
    "kernel void reduce_segments( const int numSegments, const int segmentLength,\n" 
    "        global float const *in, global float* out ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "    const int segmentId = globalId;\n" 
    "\n" 
    "    if( segmentId >= numSegments ) {\n" 
    "        return;\n" 
    "    }\n" 
    "\n" 
    "    float sum = 0;\n" 
    "    global const float *segment = in + segmentId * segmentLength;\n" 
    "    for( int i = 0; i < segmentLength; i++ ) {\n" 
    "        sum += segment[i];\n" 
    "    }\n" 
    "    out[segmentId] = sum;\n" 
    "}\n" 
    "\n" 
    "\n" 
    "";
    kernel_reduce = cl->buildKernelFromString( kernel_reduceSource, "reduce_segments", options, "cl/reduce_segments.cl" );
    // generated using cog:
    const char * kernel_activateSource =  
    "// Copyright Hugh Perkins 2015 hughperkins at gmail\n" 
    "//\n" 
    "// This Source Code Form is subject to the terms of the Mozilla Public License,\n" 
    "// v. 2.0. If a copy of the MPL was not distributed with this file, You can\n" 
    "// obtain one at http://mozilla.org/MPL/2.0/.\n" 
    "\n" 
    "// expected defines:\n" 
    "// one of: [ TANH | RELU | LINEAR | SIGMOID | SCALEDTANH ]\n" 
    "\n" 
    "#ifdef TANH\n" 
    "    #define ACTIVATION_FUNCTION(output) (tanh(output))\n" 
    "#elif defined SCALEDTANH\n" 
    "    #define ACTIVATION_FUNCTION(output) ( 1.7159f * tanh( 0.66667f * output))\n" 
    "#elif SIGMOID\n" 
    "    #define ACTIVATION_FUNCTION(output) (1.0f / (1 + exp(-output)))\n" 
    "#elif defined RELU\n" 
    "    #define ACTIVATION_FUNCTION(output) (output> 0 ? output : 0)\n" 
    "#elif defined LINEAR\n" 
    "    #define ACTIVATION_FUNCTION(output) (output)\n" 
    "#endif\n" 
    "\n" 
    "#ifdef ACTIVATION_FUNCTION // protect against not defined\n" 
    "kernel void activate( const int N, global float *inout ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "    if( globalId >= N ) {\n" 
    "        return;\n" 
    "    }\n" 
    "    inout[globalId] = ACTIVATION_FUNCTION( inout[globalId] );\n" 
    "}\n" 
    "#endif\n" 
    "\n" 
    "";
    kernel_activate = cl->buildKernelFromString( kernel_activateSource, "activate", options, "cl/activate.cl" );
    // generated using cog:
    const char * kPerElementTiledAddSource =  
    "// Copyright Hugh Perkins 2015 hughperkins at gmail\n" 
    "//\n" 
    "// This Source Code Form is subject to the terms of the Mozilla Public License,\n" 
    "// v. 2.0. If a copy of the MPL was not distributed with this file, You can\n" 
    "// obtain one at http://mozilla.org/MPL/2.0/.\n" 
    "\n" 
    "kernel void per_element_add( const int N, global float *target, global const float *source ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "    if( globalId >= N ) {\n" 
    "        return;\n" 
    "    }\n" 
    "    target[globalId] += source[globalId];\n" 
    "}\n" 
    "\n" 
    "// adds source to target\n" 
    "// tiles source as necessary, according to tilingSize\n" 
    "kernel void per_element_tiled_add( const int N, const int tilingSize, global float *target, global const float *source ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "    if( globalId >= N ) {\n" 
    "        return;\n" 
    "    }\n" 
    "    target[globalId] += source[globalId % tilingSize];\n" 
    "}\n" 
    "\n" 
    "kernel void repeated_add( const int N, const int sourceSize, const int repeatSize, global float *target, global const float *source ) {\n" 
    "    const int globalId = get_global_id(0);\n" 
    "    if( globalId >= N ) {\n" 
    "        return;\n" 
    "    }\n" 
    "    target[globalId] += source[ ( globalId / repeatSize ) % sourceSize ];\n" 
    "}\n" 
    "\n" 
    "";
    kPerElementTiledAdd = cl->buildKernelFromString( kPerElementTiledAddSource, "per_element_tiled_add", options, "cl/per_element_add.cl" );
    // [[[end]]]
}

