#pragma once

// -----------------------------------------------------------------------------
// QA integration / robustness suites.
//
// Implemented in tests/QaIntegrationTests.cpp and called from tests/TestMain.cpp.
// These are cross-cutting contract tests (realtime safety, sample-rate
// independence, block-size invariance, transport, determinism, hostile input,
// publishing thread safety) that belong to the instrument as a whole rather
// than to one DSP workstream.
// -----------------------------------------------------------------------------
void runQaIntegrationTests();
