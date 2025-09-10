#include "Axle.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Vehicle {

    void Axle::update(float steerInput, float wheelOmegaL, float wheelOmegaR, float dt) {
        // place wheels at hubs
        left.params.localOffset = { -params.halfTrack, params.hubY, params.hubZ };
        right.params.localOffset = { params.halfTrack, params.hubY, params.hubZ };

        // steering both (front)
        left.update(steerInput, wheelOmegaL, dt);
        right.update(steerInput, wheelOmegaR, dt);

        // driveshaft endpoints (from a “diff” roughly at centerline)
        leftShaft.localA = { 0.0f, params.hubY, params.hubZ };
        leftShaft.localB = left.params.localOffset;
        rightShaft.localA = { 0.0f, params.hubY, params.hubZ };
        rightShaft.localB = right.params.localOffset;
    }

    void Axle::draw(const glm::mat4& vehicleXform, GLuint shader) {
        // wheels first
        left.draw(vehicleXform, glm::mat4(1), shader, { 0.2f,0.2f,0.2f });
        right.draw(vehicleXform, glm::mat4(1), shader, { 0.2f,0.2f,0.2f });
        // then driveshafts
        leftShaft.draw(vehicleXform, shader);
        rightShaft.draw(vehicleXform, shader);
    }

} // namespace Vehicle
