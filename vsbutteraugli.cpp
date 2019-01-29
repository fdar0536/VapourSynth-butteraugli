/*
 * This is based on Google's butteraugli.
 * 
 * Reference:
 * libpnmio - https://github.com/nkkav/libpnmio
 */

#include <vector>
#include <algorithm>

#include <cstdlib>
#include <cstdint>

#include "vapoursynth/VapourSynth.h"
#include "vapoursynth/VSHelper.h"
#include "butteraugli/butteraugli/butteraugli.h"


using namespace std;
using namespace butteraugli;

static void ScoreToRgb(double score, double good_threshold,
	double bad_threshold, uint8_t &r, uint8_t &g, uint8_t &b)
{
	double heatmap[12][3] =
	{
		{ 0, 0, 0 },
		{ 0, 0, 1 },
		{ 0, 1, 1 },
		{ 0, 1, 0 }, // Good level
		{ 1, 1, 0 },
		{ 1, 0, 0 }, // Bad level
		{ 1, 0, 1 },
		{ 0.5, 0.5, 1.0 },
		{ 1.0, 0.5, 0.5 },  // Pastel colors for the very bad quality range.
		{ 1.0, 1.0, 0.5 },
		{ 1, 1, 1, },
		{ 1, 1, 1, },
	};

	if (score < good_threshold)
	{
		score = (score / good_threshold) * 0.3;
	}
	else if (score < bad_threshold)
	{
		score = 0.3 + (score - good_threshold) /
			(bad_threshold - good_threshold) * 0.15;
	}
	else
	{
		score = 0.45 + (score - bad_threshold) /
			(bad_threshold * 12) * 0.5;
	}

	static const int kTableSize = sizeof(heatmap) / sizeof(heatmap[0]);
	
	score = std::min<double>(std::max<double>(
		score * (kTableSize - 1), 0.0), kTableSize - 2);
	
	int ix = static_cast<int>(score);
	
	double mix = score - ix;
	double v;

	//r
	v = mix * heatmap[ix + 1][0] + (1 - mix) * heatmap[ix][0];
	r = static_cast<uint8_t>(255 * pow(v, 0.5) + 0.5);
	
	//g
	v = mix * heatmap[ix + 1][1] + (1 - mix) * heatmap[ix][1];
	g = static_cast<uint8_t>(255 * pow(v, 0.5) + 0.5);

	//b
	v = mix * heatmap[ix + 1][2] + (1 - mix) * heatmap[ix][2];
	b = static_cast<uint8_t>(255 * pow(v, 0.5) + 0.5);
}

void CreateHeatMapImage(const ImageF& distmap, double good_threshold,
	double bad_threshold, size_t xsize, size_t ysize,
	uint8_t *dst_r, uint8_t *dst_g, uint8_t *dst_b, int stride)
{
	for (size_t y = 0; y < ysize; ++y)
	{
		for (size_t x = 0; x < xsize; ++x)
		{
			int px = xsize * y + x;
			double d = distmap.Row(y)[x];
			ScoreToRgb(d, good_threshold, bad_threshold, dst_r[x], dst_g[x], dst_b[x]);
		}

		dst_r += stride;
		dst_g += stride;
		dst_b += stride;
	}
}

const double *NewSrgbToLinearTable()
{
	double *table = new double[256];
	for (int i = 0; i < 256; ++i)
	{
		const double srgb = i / 255.0;
		table[i] =
			255.0 * (srgb <= 0.04045 ? srgb / 12.92
				: std::pow((srgb + 0.055) / 1.055, 2.4));
	}

	return table;
} //end NewSrgbToLinearTable

void FromSrgbToLinear(const std::vector<Image8>& rgb,
	std::vector<ImageF>& linear, int background)
{
	const size_t xsize = rgb[0].xsize();
	const size_t ysize = rgb[0].ysize();
	static const double* const kSrgbToLinearTable = NewSrgbToLinearTable();

	for (int c = 0; c < 3; c++)  //first for loop
	{
		linear.push_back(ImageF(xsize, ysize));
		for (size_t y = 0; y < ysize; ++y) //second for loop
		{
			const uint8_t* const BUTTERAUGLI_RESTRICT row_rgb = rgb[c].Row(y);
			float* const BUTTERAUGLI_RESTRICT row_linear = linear[c].Row(y);
			for (size_t x = 0; x < xsize; x++) //third for loop
			{
				const int value = row_rgb[x];
				row_linear[x] = kSrgbToLinearTable[value];
			} //end third for loop
		} //end second for loop
	} //end first for loop
} //end FromSrgbToLinear

typedef struct
{
	VSNodeRef *node1;
	VSNodeRef *node2;
	const VSVideoInfo *vi;
} butteraugliData;

static void VS_CC butteraugliInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
	butteraugliData *d = (butteraugliData *)*instanceData;
	vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC butteraugliGetFrame(int n, int activationReason, void **instanceData,
	void **frameData, VSFrameContext *frameCtx, VSCore *core,
	const VSAPI *vsapi)
{
	butteraugliData *d = (butteraugliData *)*instanceData;

	if (activationReason == arInitial)
	{
		vsapi->requestFrameFilter(n, d->node1, frameCtx);
		vsapi->requestFrameFilter(n, d->node2, frameCtx);
	}
	else if (activationReason == arAllFramesReady)
	{
		const VSFrameRef *src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
		const VSFrameRef *src2 = vsapi->getFrameFilter(n, d->node2, frameCtx);
		const VSFormat *fi = d->vi->format;
		int height = vsapi->getFrameHeight(src1, 0);
		int width = vsapi->getFrameWidth(src1, 0);
		VSFrameRef *dst = vsapi->newVideoFrame(fi, width, height, src1, core);

		vector<Image8> rgb1 = CreatePlanes<uint8_t>(width, height, 3);
		vector<Image8> rgb2 = CreatePlanes<uint8_t>(width, height, 3);
		ImageF diff_map;

		int plane;
		for (plane = 0; plane < fi->numPlanes; plane++)
		{
			const uint8_t *srcp1 = vsapi->getReadPtr(src1, plane);
			const uint8_t *srcp2 = vsapi->getReadPtr(src2, plane);
			//float *dstp = (float *)vsapi->getWritePtr(dst, plane);
			int src_stride = vsapi->getStride(src1, plane);

			for (int y = 0; y < height; ++y)
			{
				memcpy(rgb1.at(plane).Row(y), srcp1, width * sizeof(uint8_t));
				memcpy(rgb2.at(plane).Row(y), srcp2, width * sizeof(uint8_t));
				srcp1 += src_stride;
				srcp2 += src_stride;
			}
		}

		vector<ImageF> linear1, linear2;
		FromSrgbToLinear(rgb1, linear1, 0);
		FromSrgbToLinear(rgb2, linear2, 0);
		double diff_value;
		if (!ButteraugliInterface(linear1, linear2, 1.0, diff_map, diff_value))
		{
			vsapi->setFilterError("butteraugli: Fail to compare.", frameCtx);
			return nullptr;
		}

		const double good_quality = ButteraugliFuzzyInverse(1.5);
		const double bad_quality = ButteraugliFuzzyInverse(0.5);

		ImageF *diff_map_ptr = &diff_map;

		uint8_t *dstp_r = vsapi->getWritePtr(dst, 0);
		uint8_t *dstp_g = vsapi->getWritePtr(dst, 1);
		uint8_t *dstp_b = vsapi->getWritePtr(dst, 2);
		int dst_stride = vsapi->getStride(dst, 0);
		CreateHeatMapImage(*diff_map_ptr, good_quality, bad_quality,
			rgb1[0].xsize(), rgb2[0].ysize(), dstp_r, dstp_g, dstp_b, dst_stride);

		VSMap *dstProps = vsapi->getFramePropsRW(dst);
		vsapi->propSetFloat(dstProps, "_Diff", diff_value, paReplace);

		vsapi->freeFrame(src1);
		vsapi->freeFrame(src2);
		return dst;
	}

	return 0;
} //end butteraugliGetFrame

// Free all allocated data on filter destruction
static void VS_CC butteraugliFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	butteraugliData *d = (butteraugliData *)instanceData;
	vsapi->freeNode(d->node1);
	vsapi->freeNode(d->node2);
	free(d);
}

// This function is responsible for validating arguments and creating a new filter
static void VS_CC butteraugliCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	butteraugliData d;
	butteraugliData *data;

	// Get a clip reference from the input arguments. This must be freed later.
	d.node1 = vsapi->propGetNode(in, "clipa", 0, 0);
	d.node2 = vsapi->propGetNode(in, "clipb", 0, 0);
	d.vi = vsapi->getVideoInfo(d.node1);

	if (!isConstantFormat(d.vi) || !isSameFormat(d.vi, vsapi->getVideoInfo(d.node2)))
	{
		vsapi->setError(out, "butteraugli: both clips must have constant format and dimensions, and the same format and dimensions");
		vsapi->freeNode(d.node1);
		vsapi->freeNode(d.node2);
		return;
	}

	if (d.vi->format->id != pfRGB24)
	{
		vsapi->setError(out, "butteraugli: only RGB24 clip supported");
		vsapi->freeNode(d.node1);
		vsapi->freeNode(d.node2);
		return;
	}

	data = (butteraugliData*)malloc(sizeof(d));
	*data = d;
	vsapi->createFilter(in, out, "Butteraugli",
						butteraugliInit, butteraugliGetFrame,
						butteraugliFree,
						fmParallel, 0, data, core);
} //end butteraugliCreate

VS_EXTERNAL_API(void)
VapourSynthPluginInit(VSConfigPlugin configFunc,
						VSRegisterFunction registerFunc,
						VSPlugin *plugin)
{
	configFunc("system.Butteraugli.butteraugli",
				"Butteraugli",
				"modified version of Google's butteraugli",
				VAPOURSYNTH_API_VERSION,
				1, plugin);
	registerFunc("butteraugli",
				"clipa:clip;"
				"clipb:clip;",
				butteraugliCreate, 0, plugin);
}