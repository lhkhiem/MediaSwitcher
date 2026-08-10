#include "engine/input/GlobalPlaylistController.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

int main() {
    std::cout << "[TEST] Starting GlobalPlaylistController State Machine Verification...\n";

    GlobalPlaylistController controller;

    // 1. Setup 2 steps (Slot 1 = A, Slot 2 = B)
    std::vector<GlobalPlaylistStep> steps = {
        {1, 0.6, "CUT"}, // Slot 1, custom duration 0.6s
        {2, 0.6, "FADE"} // Slot 2, custom duration 0.6s
    };
    controller.setSteps(steps);
    controller.setLoop(true);

    // 2. Start controller
    controller.start();
    assert(controller.isActive());
    assert(controller.currentIndex() == 0);
    assert(controller.state() == PlaylistState::Playing);
    controller.completeTransition();

    std::cout << "[TEST] Started playlist: step 0 (slot 1)\n";

    size_t expectedIndex = 0;
    int cycleCount = 0;

    // 3. Simulate 50 loop cycles
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        bool finished = controller.isCurrentItemFinished(0.6, 0.6, true, false);
        if (finished) {
            size_t oldIdx = controller.currentIndex();
            bool advanced = controller.advance(false);
            assert(advanced);
            assert(controller.state() == PlaylistState::Transitioning);

            size_t newIdx = controller.currentIndex();
            expectedIndex = (oldIdx + 1) % steps.size();

            assert(newIdx == expectedIndex);
            std::cout << "[TEST ADVANCE #" << cycleCount + 1 << "] Step " << oldIdx << " -> Step " << newIdx 
                      << " (Slot " << steps[oldIdx].slotId << " -> Slot " << steps[newIdx].slotId << ")\n";

            // Complete transition
            controller.completeTransition();
            assert(controller.state() == PlaylistState::Playing);

            cycleCount++;
        }
    }

    std::cout << "[TEST] Completed " << cycleCount << " auto-advance cycles successfully!\n";
    assert(cycleCount >= 10);

    // 4. Test Manual Next & Previous
    std::cout << "[TEST] Testing Manual Next & Previous...\n";
    
    size_t startIdx = controller.currentIndex();
    bool manualNext = controller.advance(true);
    assert(manualNext);
    size_t nextIdx = controller.currentIndex();
    assert(nextIdx == (startIdx + 1) % steps.size());
    controller.completeTransition();
    std::cout << "[TEST MANUAL NEXT] " << startIdx << " -> " << nextIdx << " SUCCESS!\n";

    bool manualPrev = controller.previous();
    assert(manualPrev);
    size_t prevIdx = controller.currentIndex();
    assert(prevIdx == startIdx);
    controller.completeTransition();
    std::cout << "[TEST MANUAL PREV] " << nextIdx << " -> " << prevIdx << " SUCCESS!\n";

    std::cout << "===========================================\n";
    std::cout << " ALL PLAYLIST STATE MACHINE TESTS PASSED!\n";
    std::cout << "===========================================\n";

    return 0;
}
