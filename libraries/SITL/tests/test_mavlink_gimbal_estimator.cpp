#include <AP_gtest.h>
#include <SITL/SIM_MAVLinkGimbalv2_Estimator.h>

const AP_HAL::HAL& hal = AP_HAL::get_HAL();

#if AP_SIM_MAVLINKGIMBALV2_ENABLED

using namespace SITL;

static mavlink_autopilot_state_for_gimbal_device_t attitude_packet()
{
    mavlink_autopilot_state_for_gimbal_device_t packet {};
    packet.q[0] = 1;
    packet.estimator_status = ESTIMATOR_ATTITUDE | ESTIMATOR_POS_HORIZ_ABS |
                              ESTIMATOR_POS_VERT_ABS | ESTIMATOR_VELOCITY_HORIZ |
                              ESTIMATOR_VELOCITY_VERT;
    return packet;
}

TEST(MAVLinkGimbalEstimator, RequiresBothFreshMessages)
{
    MAVLinkGimbalv2_Estimator estimator;
    Location location;
    EXPECT_FALSE(estimator.get_location(0, location));
    mavlink_global_position_int_t position {};
    position.lat = -350000000;
    position.lon = 1490000000;
    position.alt = 600000;
    estimator.handle_position(position, 100);
    EXPECT_FALSE(estimator.get_location(100, location));
    auto attitude = attitude_packet();
    estimator.handle_attitude(attitude, 100);
    ASSERT_TRUE(estimator.get_location(100, location));
    EXPECT_EQ(location.lat, position.lat);
    EXPECT_EQ(location.lng, position.lon);
    EXPECT_EQ(location.alt, 60000);
    EXPECT_EQ(location.get_alt_frame(), Location::AltFrame::ABSOLUTE);

    // Refreshing either stream alone must not conceal loss of the other.
    estimator.handle_attitude(attitude, 1200);
    EXPECT_FALSE(estimator.get_location(1200, location));
    estimator.handle_position(position, 1200);
    EXPECT_TRUE(estimator.get_location(1200, location));
    estimator.handle_position(position, 2300);
    EXPECT_FALSE(estimator.get_location(2300, location));
    estimator.handle_attitude(attitude, 2300);
    EXPECT_TRUE(estimator.get_location(2300, location));

    attitude.estimator_status &= ~ESTIMATOR_POS_HORIZ_ABS;
    estimator.handle_attitude(attitude, 2300);
    EXPECT_FALSE(estimator.get_location(2300, location));
    attitude = attitude_packet();
    attitude.q[0] = NAN;
    estimator.handle_attitude(attitude, 2300);
    EXPECT_FALSE(estimator.attitude_valid(2300));
    attitude.q[0] = 0;
    estimator.handle_attitude(attitude, 2300);
    EXPECT_FALSE(estimator.attitude_valid(2300));
}

TEST(MAVLinkGimbalEstimator, PredictsWithGlobalPositionVelocity)
{
    MAVLinkGimbalv2_Estimator estimator;
    auto attitude = attitude_packet();
    // These velocities must not override GLOBAL_POSITION_INT's velocities.
    attitude.vx = -50;
    attitude.vy = -50;
    attitude.vz = -50;
    estimator.handle_attitude(attitude, 100);
    mavlink_global_position_int_t position {};
    position.lat = -350000000;
    position.lon = 1490000000;
    position.alt = 600000;
    position.vx = 2000;
    position.vy = -1000;
    position.vz = 400;
    estimator.handle_position(position, 100);
    Location start, predicted;
    ASSERT_TRUE(estimator.get_location(100, start));
    ASSERT_TRUE(estimator.get_location(600, predicted));
    const Vector2f displacement = start.get_distance_NE(predicted);
    EXPECT_NEAR(displacement.x, 10, 0.02);
    EXPECT_NEAR(displacement.y, -5, 0.02);
    EXPECT_EQ(predicted.alt, 59800);
}

TEST(MAVLinkGimbalEstimator, CombinesVehicleAttitudeAndThreeEncoders)
{
    MAVLinkGimbalv2_Estimator estimator;
    Matrix3f vehicle, gimbal;
    const Vector3f encoders(0.2, -0.4, 0.6);
    EXPECT_FALSE(estimator.get_attitude(0, encoders, vehicle, gimbal));
    auto attitude = attitude_packet();
    Quaternion q;
    q.from_euler(-0.3, 0.1, 1.2);
    attitude.q[0] = q.q1;
    attitude.q[1] = q.q2;
    attitude.q[2] = q.q3;
    attitude.q[3] = q.q4;
    estimator.handle_attitude(attitude, 100);
    ASSERT_TRUE(estimator.get_attitude(100, encoders, vehicle, gimbal));
    Matrix3f expected_vehicle, expected_encoder;
    q.rotation_matrix(expected_vehicle);
    expected_encoder.from_euler312(encoders.x, encoders.y, encoders.z);
    const Matrix3f expected = expected_vehicle * expected_encoder;
    EXPECT_NEAR((vehicle.a - expected_vehicle.a).length(), 0, 1.0e-6);
    EXPECT_NEAR((gimbal.a - expected.a).length(), 0, 1.0e-6);
    EXPECT_NEAR((gimbal.b - expected.b).length(), 0, 1.0e-6);
    EXPECT_NEAR((gimbal.c - expected.c).length(), 0, 1.0e-6);
    EXPECT_FALSE(estimator.get_attitude(1101, encoders, vehicle, gimbal));
}

TEST(MAVLinkGimbalEstimator, PredictsAttitudeFromReceivedSamples)
{
    MAVLinkGimbalv2_Estimator estimator;
    auto attitude = attitude_packet();
    attitude.time_boot_us = 100000;
    estimator.handle_attitude(attitude, 100);
    Quaternion q;
    const Vector3f rate(0.1, -0.2, 0.3);
    q.from_axis_angle(rate * 0.1);
    attitude.q[0] = q.q1;
    attitude.q[1] = q.q2;
    attitude.q[2] = q.q3;
    attitude.q[3] = q.q4;
    attitude.time_boot_us = 200000;
    estimator.handle_attitude(attitude, 200);
    EXPECT_NEAR((estimator.vehicle_rates() - rate).length(), 0, 1.0e-5);
    Matrix3f vehicle, gimbal, expected;
    ASSERT_TRUE(estimator.get_attitude(300, Vector3f{}, vehicle, gimbal));
    q.from_axis_angle(rate * 0.2);
    q.rotation_matrix(expected);
    EXPECT_NEAR((gimbal.a - expected.a).length(), 0, 1.0e-5);
    EXPECT_NEAR((gimbal.b - expected.b).length(), 0, 1.0e-5);
    EXPECT_NEAR((gimbal.c - expected.c).length(), 0, 1.0e-5);
}

#endif // AP_SIM_MAVLINKGIMBALV2_ENABLED

AP_GTEST_MAIN()
