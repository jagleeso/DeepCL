// Copyright Hugh Perkins 2014 hughperkins at gmail
//
// This Source Code Form is subject to the terms of the Mozilla Public License, 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.

void kernel convolve_ints( global const int *p_boardSize, global const int *p_filterSize,
      global const int *image, global const int *filter, global int *result ) {
    int id = get_global_id(0);
    int boardSize = p_boardSize[0];
    int filterSize = p_filterSize[0];
    int boardOffset = id / (boardSize * boardSize ) * (boardSize * boardSize );
    int localid = id % (boardSize * boardSize );
    int row = localid / boardSize;
    int col = localid % boardSize;
    int halfFilterSize = filterSize >> 1;
    int sum = 0;
    int minm = max( -halfFilterSize, -row );
    int maxm = min( halfFilterSize, boardSize - 1 - row );
    int minn = max( -halfFilterSize, -col );
    int maxn = min( halfFilterSize, boardSize - 1 - col );
    int m = minm;
    while( m <= maxm ) {
        int x = ( row + m );
        int xboard = boardOffset + x * boardSize;
        int filterrowoffset = (m+halfFilterSize) * filterSize + halfFilterSize;
        int n = minn;
        while( n <= maxn ) {
            int y = col + n;
            sum += image[ xboard + y] * filter[ filterrowoffset + n ];
            n++;
        }
        m++;
    }
    result[id] = sum;
}

void kernel convolve_floats( global const int *p_boardSize, global const int *p_filterSize,
      global const float *image, global const float *filter, global float *result ) {
    int id = get_global_id(0);
    int boardSize = p_boardSize[0];
    int filterSize = p_filterSize[0];
    int boardOffset = id / (boardSize * boardSize ) * (boardSize * boardSize );
    int localid = id % (boardSize * boardSize );
    int row = localid / boardSize;
    int col = localid % boardSize;
    int halfFilterSize = filterSize >> 1;
    float sum = 0;
    int minm = max( -halfFilterSize, -row );
    int maxm = min( halfFilterSize, boardSize - 1 - row );
    int minn = max( -halfFilterSize, -col );
    int maxn = min( halfFilterSize, boardSize - 1 - col );
    int m = minm;
    while( m <= maxm ) {
        int x = ( row + m );
        int xboard = boardOffset + x * boardSize;
        int filterrowoffset = (m+halfFilterSize) * filterSize + halfFilterSize;
        int n = minn;
        while( n <= maxn ) {
            int y = col + n;
            sum += image[ xboard + y] * filter[ filterrowoffset + n ];
            n++;
        }
        m++;
    }
    result[id] = sum;
}

// receive images as a stack of images
// globalid = n * numfilters * boardsize * boardsize + filter * boardsize * boardsize + imagerow * boardsize + imagecol
//                                 globalid              globalid
//  inputboard3 1 inputboard2 1----filter 1             -> outputboard2 1   outputboard3 1
//                inputboard2 2_/\_filter 2             -> outputboard2 2
//  inputboard3 2 inputboard2 3    filter 1             -> outputboard2 3   outputboard3 2
//                inputboard2 4    filter 2             -> outputboard2 4
//
// each outputboard is only written once, by a combination of:
// - one inputboard3
// - one filter
// each inputboard3 is mapped to each filter once, each time writing to one outputboard
//
// images is:
//       numimages * numinputplanes * boardsizesquared
// filters is:
//       numfilters * numinputplanes * boardsizesquared
// outputs is:
//       numimages * numfilters * boardsizesquared
void kernel convolve_imagecubes_int( global const int *p_numInputPlanes, global const int *p_numFilters, 
      global const int *p_boardSize, global const int *p_filterSize,
      global const int *images, global const int *filters, global int *results ) {
    int globalId = get_global_id(0);

    int numInputPlanes = p_numInputPlanes[0];
    int numFilters = p_numFilters[0];
    int boardSize = p_boardSize[0];
    int filterSize = p_filterSize[0];
    int boardSizeSquared = boardSize * boardSize;

    int outputBoard2Id = globalId / boardSizeSquared;
    int filterId = outputBoard2Id % numFilters;
    int inputBoard3Id = outputBoard2Id / numFilters;

    int filterOffset = filterId * filterSize * filterSize;
    int inputBoard3Offset = inputBoard3Id * numInputPlanes * boardSizeSquared;

    // intraboard coords
    int localid = globalId % boardSizeSquared;
    int row = localid / boardSize;
    int col = localid % boardSize;

    int halfFilterSize = filterSize >> 1;
    int sum = 0;
    int minm = max( -halfFilterSize, -row );
    int maxm = min( halfFilterSize, boardSize - 1 - row );
    int minn = max( -halfFilterSize, -col );
    int maxn = min( halfFilterSize, boardSize - 1 - col );
    int plane = 0;
    while( plane < numInputPlanes ) {
        int inputBoardOffset = inputBoard3Offset + plane * boardSizeSquared;
        int filterPlaneOffset = filterOffset + plane * filterSize * filterSize;
        int m = minm;
        while( m <= maxm ) {
            int y = row + m;
            int inputboardrowoffset = inputBoardOffset + y * boardSize;
            int filterrowoffset = filterPlaneOffset + (m+halfFilterSize) * filterSize + halfFilterSize;
            int n = minn;
            while( n <= maxn ) {
                int x = col + n;
                sum += images[ inputboardrowoffset + x] * filters[ filterrowoffset + n ];
                n++;
            }
            m++;
        }
        plane++;
    }
    results[globalId] = sum;
}

void kernel convolve_imagecubes_float( global const int *p_numInputPlanes, global const int *p_numFilters, 
      global const int *p_boardSize, global const int *p_filterSize,
      global const float *images, global const float *filters, global float *results ) {
    int globalId = get_global_id(0);

    int numInputPlanes = p_numInputPlanes[0];
    int numFilters = p_numFilters[0];
    int boardSize = p_boardSize[0];
    int filterSize = p_filterSize[0];
    int boardSizeSquared = boardSize * boardSize;

    int outputBoard2Id = globalId / boardSizeSquared;
    int filterId = outputBoard2Id % numFilters;
    int inputBoard3Id = outputBoard2Id / numFilters;

    int filterOffset = filterId * filterSize * filterSize;
    int inputBoard3Offset = inputBoard3Id * numInputPlanes * boardSizeSquared;

    // intraboard coords
    int localid = globalId % boardSizeSquared;
    int row = localid / boardSize;
    int col = localid % boardSize;

    int halfFilterSize = filterSize >> 1;
    float sum = 0;
    int minm = max( -halfFilterSize, -row );
    int maxm = min( halfFilterSize, boardSize - 1 - row );
    int minn = max( -halfFilterSize, -col );
    int maxn = min( halfFilterSize, boardSize - 1 - col );
    int plane = 0;
    while( plane < numInputPlanes ) {
        int inputBoardOffset = inputBoard3Offset + plane * boardSizeSquared;
        int filterPlaneOffset = filterOffset + plane * filterSize * filterSize;
        int m = minm;
        while( m <= maxm ) {
            int y = row + m;
            int inputboardrowoffset = inputBoardOffset + y * boardSize;
            int filterrowoffset = filterPlaneOffset + (m+halfFilterSize) * filterSize + halfFilterSize;
            int n = minn;
            while( n <= maxn ) {
                int x = col + n;
                sum += images[ inputboardrowoffset + x] * filters[ filterrowoffset + n ];
                n++;
            }
            m++;
        }
        plane++;
    }
    results[globalId] = sum;
}


    