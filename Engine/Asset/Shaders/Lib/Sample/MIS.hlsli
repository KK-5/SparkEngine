#ifndef SPARK_LIB_MIS_HLSLI
#define SPARK_LIB_MIS_HLSLI

// Power heuristic (beta = 2). Densities must use the same measure. For one
// sample per technique, pass their PDFs; for n samples, pass n * PDF instead.
float MISPowerHeuristic(float sampledPdf, float competingPdf)
{
    const float scale = max(sampledPdf, competingPdf);
    if (scale <= 0.0)
    {
        return 0.0;
    }
    // Normalize before squaring to avoid overflow with very narrow proposals.
    const float a = sampledPdf / scale;
    const float b = competingPdf / scale;
    return a * a / (a * a + b * b);
}

#endif // SPARK_LIB_MIS_HLSLI
