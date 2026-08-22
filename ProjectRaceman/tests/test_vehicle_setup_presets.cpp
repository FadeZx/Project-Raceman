// Offline behaviour check for the setup preset tables in VehicleConfig.cpp.
// No editor, no physics step: this only asks what a car is configured to be
// after applyVehicleSetupPresets has run, which is what the runtime drives.
//
// The tier table has been a no-op before. Every value the Simulation level
// clamped was already sitting on the clamp, so a car labelled Simulation
// behaved exactly like one labelled Arcade and nothing said so. These checks
// exist so that failure is loud rather than silent.
#include "VehicleConfig.h"

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

static std::string F(const char* name, float value) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s %.3f", name, value);
    return buffer;
}

// A GT car on soft slicks, which is the combination the tiers are tuned
// against. Everything else is left at its default.
static VehicleConfig MakeGtCar(const char* simulationLevel) {
    VehicleConfig config;
    config.setup.enabled = true;
    config.setup.tireCompound = "SlickSoft";
    config.setup.drivetrainLayout = "RWD";
    config.setup.handlingBalance = "Neutral";
    config.setup.stabilityAssist = 0.0f;
    config.setup.simulationLevel = simulationLevel;
    applyVehicleSetupPresets(config);
    return config;
}

// ---------------------------------------------------------------------------

// The lateral model weighs a corner demand that cannot exceed 1.0 against
// lateralGrip / 5. Any car whose lateralGrip stays above 5 has a cornering
// limit the driver physically cannot reach, whatever else is tuned.
static void TestSimulationLimitIsReachable() {
    std::printf("\n1. A Simulation car has a cornering limit the driver can reach\n");
    const VehicleConfig sim = MakeGtCar("Simulation");
    const VehicleConfig arcade = MakeGtCar("Arcade");

    Check(sim.tireGrip.lateralGrip / 5.0f < 0.95f,
          "simulation grip limit is inside the demand range",
          F("limit", sim.tireGrip.lateralGrip / 5.0f));
    Check(arcade.tireGrip.lateralGrip / 5.0f > 1.0f,
          "arcade grip limit stays out of reach on purpose",
          F("limit", arcade.tireGrip.lateralGrip / 5.0f));
}

static void TestTiersAreOrdered() {
    std::printf("\n2. The three tiers are ordered, not three names for one car\n");
    const VehicleConfig arcade = MakeGtCar("Arcade");
    const VehicleConfig simcade = MakeGtCar("Simcade");
    const VehicleConfig sim = MakeGtCar("Simulation");

    Check(arcade.tireGrip.lateralGrip > simcade.tireGrip.lateralGrip &&
              simcade.tireGrip.lateralGrip > sim.tireGrip.lateralGrip,
          "lateral grip falls with each tier",
          F("arcade", arcade.tireGrip.lateralGrip) + F(" simcade", simcade.tireGrip.lateralGrip) +
              F(" sim", sim.tireGrip.lateralGrip));

    Check(arcade.tireGrip.minTractionScale > simcade.tireGrip.minTractionScale &&
              simcade.tireGrip.minTractionScale > sim.tireGrip.minTractionScale,
          "a slide costs more with each tier",
          F("arcade", arcade.tireGrip.minTractionScale) + F(" sim", sim.tireGrip.minTractionScale));

    Check(arcade.tireDynamics.counterSteerTorque > sim.tireDynamics.counterSteerTorque,
          "arcade catches slides for the driver, simulation does not",
          F("arcade", arcade.tireDynamics.counterSteerTorque) +
              F(" sim", sim.tireDynamics.counterSteerTorque));

    Check(arcade.tireDynamics.velocityAlignmentRate > sim.tireDynamics.velocityAlignmentRate,
          "arcade walks the car back onto its velocity vector, simulation does not",
          F("arcade", arcade.tireDynamics.velocityAlignmentRate) +
              F(" sim", sim.tireDynamics.velocityAlignmentRate));
}

static void TestSimulationOwnsTheDrivetrain() {
    std::printf("\n3. Simulation hands the drivetrain to the driver\n");
    const VehicleConfig arcade = MakeGtCar("Arcade");
    const VehicleConfig sim = MakeGtCar("Simulation");

    Check(sim.transmission.mode == TransmissionConfig::Mode::Manual,
          "simulation shifts manually", "");
    Check(arcade.transmission.mode == TransmissionConfig::Mode::Automatic,
          "arcade shifts itself", "");
    Check(sim.arcadeHandling.engineDrivenAcceleration,
          "simulation drive comes from the torque curve", "");
    Check(!arcade.arcadeHandling.engineDrivenAcceleration,
          "arcade keeps its flat pull", "");
    Check(sim.tireGrip.combinedSlip,
          "simulation spends one friction budget", "");
    Check(!arcade.tireGrip.combinedSlip,
          "arcade keeps braking and cornering independent", "");
}

static void TestSimulationRotatesFromTheTyres() {
    std::printf("\n4. Simulation makes the tyres decide the rotation\n");
    const VehicleConfig arcade = MakeGtCar("Arcade");
    const VehicleConfig sim = MakeGtCar("Simulation");

    Check(sim.yawDynamics.physicalYaw,
          "simulation builds yaw from axle slip angles", "");
    Check(!arcade.yawDynamics.physicalYaw,
          "arcade lets steering command yaw directly", "");

    // Suppressing this was how the old model kept a slide alive, and it cost
    // the car its ordinary cornering: the velocity lagged the heading so far
    // that a GT car sat sideways on full grip.
    Check(sim.tireDynamics.velocityAlignmentRate > 1.0f,
          "a simulation car still tracks its nose when it is not sliding",
          F("rate", sim.tireDynamics.velocityAlignmentRate));
}

// The tier scales values the compound and balance tables own. Those tables
// rebuild them first, so a second apply must land in the same place. If it
// does not, pressing Apply in the editor twice quietly halves the car.
static void TestApplyIsIdempotent() {
    std::printf("\n5. Applying the setup twice gives the same car\n");
    VehicleConfig once = MakeGtCar("Simulation");
    VehicleConfig twice = once;
    applyVehicleSetupPresets(twice);

    Check(std::fabs(once.tireGrip.lateralGrip - twice.tireGrip.lateralGrip) < 0.001f,
          "lateral grip is stable across a repeated apply",
          F("once", once.tireGrip.lateralGrip) + F(" twice", twice.tireGrip.lateralGrip));
    Check(std::fabs(once.tireDynamics.yawFromRearSlip - twice.tireDynamics.yawFromRearSlip) < 0.001f,
          "yaw response is stable across a repeated apply",
          F("once", once.tireDynamics.yawFromRearSlip) + F(" twice", twice.tireDynamics.yawFromRearSlip));
}

// A Custom compound means the author owns the tyre numbers. Scaling them on
// every apply would walk a hand-tuned car into the floor, so the tier clamps
// to an absolute bound there instead.
static void TestCustomCompoundIsNotWalkedDown() {
    std::printf("\n6. A custom compound is clamped, not scaled repeatedly\n");
    VehicleConfig config;
    config.setup.enabled = true;
    config.setup.tireCompound = "Custom";
    config.setup.handlingBalance = "Custom";
    config.setup.simulationLevel = "Simulation";
    config.tireGrip.lateralGrip = 3.0f;
    config.tireDynamics.yawFromRearSlip = 60.0f;

    applyVehicleSetupPresets(config);
    const float afterOne = config.tireGrip.lateralGrip;
    const float yawAfterOne = config.tireDynamics.yawFromRearSlip;
    for (int i = 0; i < 5; ++i) {
        applyVehicleSetupPresets(config);
    }

    Check(std::fabs(afterOne - config.tireGrip.lateralGrip) < 0.001f,
          "six applies leave custom lateral grip where one did",
          F("first", afterOne) + F(" sixth", config.tireGrip.lateralGrip));
    Check(std::fabs(yawAfterOne - config.tireDynamics.yawFromRearSlip) < 0.001f,
          "six applies leave custom yaw response where one did",
          F("first", yawAfterOne) + F(" sixth", config.tireDynamics.yawFromRearSlip));
    Check(config.tireGrip.lateralGrip <= 3.0f,
          "a custom car below the bound is left alone rather than raised",
          F("grip", config.tireGrip.lateralGrip));
}

// Presets only run when the setup block is driving the car. A config that has
// opted out must come back exactly as authored.
static void TestDisabledSetupChangesNothing() {
    std::printf("\n7. A car with setup presets off is left untouched\n");
    VehicleConfig config;
    config.setup.enabled = false;
    config.setup.simulationLevel = "Simulation";
    config.tireGrip.lateralGrip = 7.8f;
    config.transmission.mode = TransmissionConfig::Mode::Automatic;

    applyVehicleSetupPresets(config);

    Check(std::fabs(config.tireGrip.lateralGrip - 7.8f) < 0.001f,
          "authored grip survives", F("grip", config.tireGrip.lateralGrip));
    Check(config.transmission.mode == TransmissionConfig::Mode::Automatic,
          "authored gearbox mode survives", "");
}

// ---------------------------------------------------------------------------

int main() {
    std::printf("Vehicle setup preset checks\n");
    std::printf("===========================\n");

    TestSimulationLimitIsReachable();
    TestTiersAreOrdered();
    TestSimulationOwnsTheDrivetrain();
    TestSimulationRotatesFromTheTyres();
    TestApplyIsIdempotent();
    TestCustomCompoundIsNotWalkedDown();
    TestDisabledSetupChangesNothing();

    std::printf("\n==========================\n");
    if (g_failures == 0) {
        std::printf("ALL PASS (0 failures)\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
