/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "test.h"

#include "fixtures/image_fixture.h"
#include "mocks/mock_context.h"

using namespace NEO;

typedef ::testing::Test gen8ImageTests;

GEN8TEST_F(gen8ImageTests, appendSurfaceStateParamsDoesNothing) {
    typedef typename FamilyType::RENDER_SURFACE_STATE RENDER_SURFACE_STATE;
    MockContext context;
    auto image = std::unique_ptr<Image>(ImageHelper<Image1dDefaults>::create(&context));
    auto surfaceStateBefore = FamilyType::cmdInitRenderSurfaceState;
    auto surfaceStateAfter = FamilyType::cmdInitRenderSurfaceState;
    auto imageHw = static_cast<ImageHw<FamilyType> *>(image.get());

    EXPECT_EQ(0, memcmp(&surfaceStateBefore, &surfaceStateAfter, sizeof(RENDER_SURFACE_STATE)));

    imageHw->appendSurfaceStateParams(&surfaceStateAfter);

    EXPECT_EQ(0, memcmp(&surfaceStateBefore, &surfaceStateAfter, sizeof(RENDER_SURFACE_STATE)));
}
