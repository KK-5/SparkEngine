#ifndef SPARK_LIB_TRIANGLE_SAMPLING_HLSLI
#define SPARK_LIB_TRIANGLE_SAMPLING_HLSLI

// Maps a uniform sample in [0, 1)^2 to a point uniformly distributed over a triangle.
// Returns (weight1, weight2), matching ray query and InterpolateGeometrySurface;
// weight0 is 1 - weight1 - weight2. squareSample may come from SobolBurley2D.
float2 SampleUniformTriangle(float2 squareSample)
{
    const float edgeWeight = sqrt(squareSample.x);
    return float2(edgeWeight * (1.0 - squareSample.y), edgeWeight * squareSample.y);
}

// Density with respect to physical triangle area, not barycentric coordinates or
// solid angle. Use WorldTriangle.Area for a world-space surface sample. This is
// conditional on the triangle already being selected; selection probabilities
// are accounted for separately. Zero-area triangles have no valid area sample.
float UniformTriangleAreaPdf(float area)
{
    if (area <= 0.0)
    {
        return 0.0;
    }
    return 1.0 / area;
}

#endif // SPARK_LIB_TRIANGLE_SAMPLING_HLSLI
