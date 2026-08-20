// Offline behaviour check for VehicleEngineState. No editor, no audio device.
#include "VehicleEngineState.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raceman::physics;

static int g_failures = 0;

static void Check(bool condition, const std::string& label, const std::string& detail) {
    std::printf("  [%s] %s%s%s\n", condition ? "PASS" : "FAIL", label.c_str(),
                detail.empty() ? "" : "  -> ", detail.c_str());
    if (!condition) ++g_failures;
}

static VehicleEngineTuning MakeTuning() {
    VehicleEngineTuning t;
    t.idleRpm = 900.0f;
    t.redlineRpm = 6000.0f;
    t.inertia = 0.28f;
    return t;
}

static VehicleEngineInput MakeInput() {
    VehicleEngineInput in;
    in.deltaTime = 1.0f / 60.0f;
    in.commandedAcceleration = 18.0f;
    in.targetRpmFromGearing = 900.0f;
    return in;
}

// ---------------------------------------------------------------------------

static void TestIdleIsStable() {
    std::printf("\n1. Idle in neutral holds at idle RPM\n");
    VehicleEngineState s; VehicleEngineTuning t = MakeTuning();
    VehicleEngineInput in = MakeInput();
    in.gear = 0; in.throttle = 0.0f;
    for (int i = 0; i < 300; ++i) s.Update(t, in);
    Check(std::fabs(s.rpm - 900.0f) < 1.0f, "rpm stays at idle",
          "rpm=" + std::to_string(s.rpm));
    Check(s.load < 0.01f, "load is zero when coasting", "load=" + std::to_string(s.load));
}

static void TestRevInNeutral() {
    std::printf("\n2. Revving in neutral (impossible before this change)\n");
    VehicleEngineState s; VehicleEngineTuning t = MakeTuning();
    VehicleEngineInput in = MakeInput();
    in.gear = 0;

    in.throttle = 1.0f;
    float rpmAfter100ms = 0.0f;
    for (int i = 0; i < 60; ++i) { s.Update(t, in); if (i == 5) rpmAfter100ms = s.rpm; }
    Check(s.rpm > 5000.0f, "throttle in neutral revs the engine", "rpm=" + std::to_string(s.rpm));
    Check(rpmAfter100ms < s.rpm, "it ramps rather than snapping",
          "rpm@100ms=" + std::to_string(rpmAfter100ms) + " rpm@1s=" + std::to_string(s.rpm));

    const float revvedTo = s.rpm;
    in.throttle = 0.0f;
    for (int i = 0; i < 120; ++i) s.Update(t, in);
    Check(s.rpm < revvedTo * 0.3f, "closing the throttle drops it back",
          "rpm=" + std::to_string(s.rpm));
}

static void TestInertiaVsSnap() {
    std::printf("\n3. Clutch engaged tracks gearing tightly, clutch out does not\n");
    VehicleEngineTuning t = MakeTuning();

    VehicleEngineState locked; VehicleEngineInput li = MakeInput();
    li.gear = 3; li.throttle = 1.0f; li.targetRpmFromGearing = 4000.0f;
    for (int i = 0; i < 30; ++i) locked.Update(t, li);

    VehicleEngineState free; VehicleEngineInput fi = MakeInput();
    fi.gear = 0; fi.throttle = 1.0f;
    for (int i = 0; i < 6; ++i) free.Update(t, fi);

    Check(std::fabs(locked.rpm - 4000.0f) < 50.0f, "in gear, rpm converges on the gearing target",
          "rpm=" + std::to_string(locked.rpm));
    Check(free.rpm < 3000.0f, "free revving is inertia limited, not instant",
          "rpm@100ms=" + std::to_string(free.rpm));
}

static void TestDownshiftFlare() {
    std::printf("\n4. Downshift flare\n");
    VehicleEngineState s; VehicleEngineTuning t = MakeTuning();
    VehicleEngineInput in = MakeInput();
    in.gear = 4; in.throttle = 0.3f; in.targetRpmFromGearing = 2500.0f;
    for (int i = 0; i < 60; ++i) s.Update(t, in);
    const float before = s.rpm;

    // Shift down: clutch out for the shift, then a higher gearing target.
    in.shifting = true; in.gear = 3; in.throttle = 0.8f;
    for (int i = 0; i < 12; ++i) s.Update(t, in);
    const float duringShift = s.rpm;

    in.shifting = false; in.targetRpmFromGearing = 3800.0f;
    for (int i = 0; i < 60; ++i) s.Update(t, in);

    Check(duringShift > before, "rpm rises while the clutch is out",
          "before=" + std::to_string(before) + " during=" + std::to_string(duringShift));
    Check(std::fabs(s.rpm - 3800.0f) < 80.0f, "then settles on the new gear",
          "rpm=" + std::to_string(s.rpm));
}

static void TestLimiterBounces() {
    std::printf("\n5. Rev limiter bounces instead of pinning flat\n");
    VehicleEngineState s; VehicleEngineTuning t = MakeTuning();
    VehicleEngineInput in = MakeInput();
    in.gear = 2; in.throttle = 1.0f; in.targetRpmFromGearing = 6000.0f;

    int cutTransitions = 0; bool wasCut = false;
    float minRpm = 1e9f, maxRpm = 0.0f;
    for (int i = 0; i < 300; ++i) {
        s.Update(t, in);
        if (i > 60) { // let it climb to the limiter first
            minRpm = (s.rpm < minRpm) ? s.rpm : minRpm;
            maxRpm = (s.rpm > maxRpm) ? s.rpm : maxRpm;
            if (s.limiterCut != wasCut) { ++cutTransitions; wasCut = s.limiterCut; }
        }
    }
    Check(cutTransitions >= 4, "ignition cuts and relights repeatedly",
          "transitions=" + std::to_string(cutTransitions));
    Check(maxRpm - minRpm > 100.0f, "rpm oscillates rather than sitting flat",
          "swing=" + std::to_string(maxRpm - minRpm) + " rpm");
    Check(s.rpm <= 6000.0f + 0.01f, "never exceeds redline", "rpm=" + std::to_string(s.rpm));
}

static void TestLaunchEventFires() {
    std::printf("\n6. Shift events, including the launch 0->1 that used to be silent\n");
    VehicleEngineState s; VehicleEngineTuning t = MakeTuning();
    VehicleEngineInput in = MakeInput();
    in.gear = 0;
    s.Update(t, in);
    s.ClearShiftEvents();

    in.gear = 1; s.Update(t, in);
    bool sawLaunch = false;
    for (int i = 0; i < s.shiftEventCount; ++i)
        if (s.shiftEvents[i].kind == EngineShiftKind::Launch) sawLaunch = true;
    Check(sawLaunch, "0 -> 1 emits a Launch event", "count=" + std::to_string(s.shiftEventCount));

    s.ClearShiftEvents();
    in.gear = 2; s.Update(t, in);
    in.gear = 3; s.Update(t, in);
    Check(s.shiftEventCount == 2, "two shifts in two fixed steps both survive",
          "count=" + std::to_string(s.shiftEventCount));

    s.ClearShiftEvents();
    in.gear = 2; s.Update(t, in);
    Check(s.shiftEventCount == 1 && s.shiftEvents[0].kind == EngineShiftKind::Down,
          "downshift is classified correctly", "");
}

static void TestLoadDiscriminates() {
    std::printf("\n7. Load separates pulling from coasting and from wheelspin\n");
    VehicleEngineTuning t = MakeTuning();

    VehicleEngineState pulling; VehicleEngineInput pi = MakeInput();
    pi.gear = 2; pi.throttle = 1.0f; pi.targetRpmFromGearing = 3000.0f;
    float speed = 10.0f;
    for (int i = 0; i < 120; ++i) {
        pi.previousSpeed = speed; speed += 18.0f * pi.deltaTime; pi.speed = speed;
        pulling.Update(t, pi);
    }

    VehicleEngineState coasting; VehicleEngineInput ci = MakeInput();
    ci.gear = 2; ci.throttle = 0.0f; ci.targetRpmFromGearing = 3000.0f;
    speed = 30.0f;
    for (int i = 0; i < 120; ++i) {
        ci.previousSpeed = speed; speed -= 2.0f * ci.deltaTime; ci.speed = speed;
        coasting.Update(t, ci);
    }

    VehicleEngineState spinning; VehicleEngineInput si = MakeInput();
    si.gear = 1; si.throttle = 1.0f; si.targetRpmFromGearing = 4000.0f; si.wheelspin = 1.0f;
    speed = 5.0f;
    for (int i = 0; i < 120; ++i) {
        si.previousSpeed = speed; speed += 1.0f * si.deltaTime; si.speed = speed;
        spinning.Update(t, si);
    }

    // Regression: top gear near max speed. The car is barely accelerating
    // because drag has caught up, but wide-open throttle means the engine is at
    // full load. Deriving load from acceleration made it fade out here.
    VehicleEngineState topGear; VehicleEngineInput ti = MakeInput();
    // Below redline: at redline the limiter correctly zeroes load, which is a
    // different behaviour from the one under test here.
    ti.gear = 6; ti.throttle = 1.0f; ti.targetRpmFromGearing = 5400.0f;
    speed = 54.0f;
    for (int i = 0; i < 180; ++i) {
        ti.previousSpeed = speed; speed += 0.15f * ti.deltaTime; ti.speed = speed; // almost flat
        topGear.Update(t, ti);
    }

    Check(pulling.load > 0.8f, "hard acceleration reads as high load",
          "load=" + std::to_string(pulling.load));
    Check(topGear.load > 0.8f, "WOT in top gear stays at full load despite barely accelerating",
          "load=" + std::to_string(topGear.load));
    Check(coasting.load < 0.05f, "closed throttle reads as overrun",
          "load=" + std::to_string(coasting.load));
    Check(spinning.load < pulling.load * 0.6f, "wheelspin unloads the engine",
          "spin=" + std::to_string(spinning.load) + " pull=" + std::to_string(pulling.load));
}

static void TestTurboSpool() {
    std::printf("\n8. Turbo spools with lag, supercharger does not\n");
    VehicleEngineTuning t = MakeTuning();
    t.turboEnabled = true;

    VehicleEngineState turbo; VehicleEngineInput in = MakeInput();
    in.gear = 3; in.throttle = 1.0f; in.targetRpmFromGearing = 5000.0f;
    float speed = 20.0f;
    float boostEarly = -1.0f;
    for (int i = 0; i < 180; ++i) {
        in.previousSpeed = speed; speed += 18.0f * in.deltaTime; in.speed = speed;
        turbo.Update(t, in);
        if (i == 6) boostEarly = turbo.boost;
    }
    Check(boostEarly < 0.35f, "boost lags at first", "boost@100ms=" + std::to_string(boostEarly));
    Check(turbo.boost > 0.6f, "then builds", "boost=" + std::to_string(turbo.boost));

    VehicleEngineTuning st = t; st.supercharger = true;
    VehicleEngineState sc; VehicleEngineInput si = MakeInput();
    si.gear = 3; si.throttle = 1.0f; si.targetRpmFromGearing = 5000.0f;
    speed = 20.0f;
    for (int i = 0; i < 180; ++i) {
        si.previousSpeed = speed; speed += 18.0f * si.deltaTime; si.speed = speed;
        sc.Update(st, si);
    }
    Check(sc.boost > 0.6f, "supercharger reaches boost too", "boost=" + std::to_string(sc.boost));

    VehicleEngineTuning nt = MakeTuning();
    VehicleEngineState na; VehicleEngineInput ni = MakeInput();
    ni.gear = 3; ni.throttle = 1.0f; ni.targetRpmFromGearing = 5000.0f;
    for (int i = 0; i < 120; ++i) na.Update(nt, ni);
    Check(na.boost == 0.0f, "naturally aspirated stays at zero boost", "");
}

static void TestFrameRateIndependence() {
    std::printf("\n9. Frame-rate independence (the old lerp overshot badly)\n");
    VehicleEngineTuning t = MakeTuning();

    VehicleEngineState fast; VehicleEngineInput fi = MakeInput();
    fi.gear = 0; fi.throttle = 1.0f; fi.deltaTime = 1.0f / 240.0f;
    for (int i = 0; i < 240; ++i) fast.Update(t, fi);

    VehicleEngineState slow; VehicleEngineInput si = MakeInput();
    si.gear = 0; si.throttle = 1.0f; si.deltaTime = 1.0f / 30.0f;
    for (int i = 0; i < 30; ++i) slow.Update(t, si);

    const float diff = std::fabs(fast.rpm - slow.rpm);
    Check(diff < 60.0f, "1 second at 240Hz and at 30Hz agree",
          "240Hz=" + std::to_string(fast.rpm) + " 30Hz=" + std::to_string(slow.rpm)
          + " diff=" + std::to_string(diff));
}

int main() {
    std::printf("VehicleEngineState behaviour checks\n");
    std::printf("===================================\n");
    TestIdleIsStable();
    TestRevInNeutral();
    TestInertiaVsSnap();
    TestDownshiftFlare();
    TestLimiterBounces();
    TestLaunchEventFires();
    TestLoadDiscriminates();
    TestTurboSpool();
    TestFrameRateIndependence();
    std::printf("\n===================================\n");
    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
