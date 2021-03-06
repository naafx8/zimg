#include "common/make_unique.h"
#include "common/pixel.h"
#include "graph/copy_filter.h"

#include "gtest/gtest.h"
#include "filter_validator.h"
#include "mock_filter.h"

TEST(CopyFilterTest, test_copy_filter)
{
	const zimg::PixelType types[] = { zimg::PixelType::BYTE, zimg::PixelType::WORD, zimg::PixelType::HALF, zimg::PixelType::FLOAT };
	const unsigned w = 591;
	const unsigned h = 333;

	const char *expected_sha1[][3] = {
		{ "b7399d798c5f96b4c9ac4c6cccd4c979468bdc7a" },
		{ "43362943f1de4b51f45679a0c460f55c8bd8d2f2" },
		{ "1a25ec59d5708d3bfc36d87b05f6d7625d4a3d24" },
		{ "078016e8752bcfb63b16c86b4ae212a51579f028" }
	};

	for (unsigned x = 0; x < 4; ++x) {
		SCOPED_TRACE(static_cast<int>(types[x]));

		zimg::graph::CopyFilter copy{ w, h, types[x] };

		FilterValidator validator{ &copy, w, h, types[x] };
		validator.set_sha1(expected_sha1[x]);
		validator.validate();
	}
}
