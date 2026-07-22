# trikRos2 — ROS 2 integration with the TRIK controller

`trikRos2` is a module (dynamic library `libtrikRos2.so`) that brings up a ROS 2
node inside the `trikGui` process running on the TRIK controller. The node gives
the host (operator's computer, another robot, etc.) access to the controller's
sensors, encoders, and IMU (accelerometer and gyroscope) via ROS 2 topics, and
also allows controlling motors and servos by publishing commands to the
corresponding topics.

The module does not create a separate "hardware" instance — it receives the
already-existing `BrickInterface` that `trikGui` works with, so only one
process ever works with the hardware at a time and there are no port access
conflicts.

## Contents

- [Compatibility](#compatibility)
- [Quick start](#quick-start)
- [Starting and stopping the node on the controller](#starting-and-stopping-the-node-on-the-controller)
- [Configuration file ros2_config.xml](#configuration-file-ros2_configxml)
- [Topics: publishers and subscribers](#topics-publishers-and-subscribers)
- [Message formats](#message-formats)
- [Default port table](#default-port-table)
- [Host ⇄ controller networking](#host--controller-networking)
- [Host-side code examples](#host-side-code-examples)

## Compatibility

The node is built for **ROS 2 Humble Hawksbill** and uses the
**`rmw_cyclonedds_cpp`** (CycloneDDS) RMW implementation. For the host and the
controller to discover each other and exchange messages, the host (and any
other device that needs to interact with the TRIK controller over ROS 2) must:

- use **ROS 2 Humble** (other ROS 2 versions are incompatible at the message
  ABI level and, generally, in edge cases at the DDS wire-protocol level too —
  this is not guaranteed to work);
- use the **same RMW**, i.e. the environment variable
  `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`. If the host uses the default RMW
  (`rmw_fastrtps_cpp`), the nodes most likely will not see each other;
- use a shared `ROS_DOMAIN_ID` (`0` by default on both sides, unless changed
  explicitly).

The standard message types used by the module — `std_msgs`, `sensor_msgs`,
`geometry_msgs` — come from the base ROS 2 distribution.

## Quick start

1. Make sure `/home/root/trik/ros2_config.xml` on the controller is configured
   with the ports you need (see [below](#configuration-file-ros2_configxml)).
   If the file is missing, the node starts with all ports enabled by default.
2. On the controller: file menu → run `libtrikRos2.so.1`. While it is running,
   the ROS 2 node `trik` is active and visible on the network.
3. On the host: `ros_domain_id` and `rmw_implementation` must match the
   controller (see [Networking](#host--controller-networking)).
4. Check that the node is visible:
   ```bash
   ros2 node list          # should show the /trik node
   ros2 topic list         # list of sensor/motor/IMU topics
   ros2 topic echo /sensors/a1
   ```
5. Write your own ROS 2 node on the host that subscribes to the sensor topics
   you need and publishes commands to the motor/servo topics (see
   [examples](#host-side-code-examples)).

## Starting and stopping the node on the controller

The module is packaged as a regular TRIK C++ program (`TrikCppProgram`), so it
is started and stopped the same way as any compiled `.so` module:

- **Start** — select `libtrikRos2.so.1` in the file menu on the controller and
  run it. On startup, the module:
  - reads the configuration from `/home/root/trik/ros2_config.xml` (or falls
    back to the default settings if the file is missing/corrupted);
  - initializes `rclcpp` (if not already initialized) and creates a node named
    `trik`;
  - sets up publishers and subscribers according to the configuration;
  - starts a data-publishing timer at the `rate_hz` frequency from the
    configuration;
  - spins a `SingleThreadedExecutor` in a separate thread.
- **Stop** — a regular stop of a running program on the controller (the
  "Stop" button / returning to the program list, same as for any other running
  module). On stop, the node cleanly terminates the `executor.spin()` thread,
  destroys the node object, and calls `rclcpp::shutdown()`.

While the module is not running, the ROS 2 node does not exist and no topics
are available.

## Configuration file ros2_config.xml

Path on the controller: **`/home/root/trik/ros2_config.xml`**.

The file is read once, **at module startup**. If the node is already running
when the configuration is changed, it must be **stopped and started again**
(via the file menu) for the changes to take effect.

If the file is missing, cannot be opened, or contains invalid XML (wrong root
element, etc.) — the built-in default configuration is used: **all ports
enabled**, `rate_hz = 20.0` (see also the [table](#default-port-table)).

### File structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ros2config>

  <!-- rate_hz — data publishing frequency -->
  <params rate_hz="20.0"/>

  <!-- Sensors: publish sensor_msgs/Range to the host -->
  <sensors>
    <sensor port="A1" topic="sensors/a1"/>
    <sensor port="A2" topic="sensors/a2"/>
    <sensor port="A3" topic="sensors/a3"/>
    <sensor port="A4" topic="sensors/a4"/>
    <sensor port="A5" topic="sensors/a5"/>
    <sensor port="A6" topic="sensors/a6"/>
    <sensor port="D1" topic="sensors/d1"/>
    <sensor port="D2" topic="sensors/d2"/>
    <sensor port="D3" topic="sensors/d3"/>
  </sensors>

  <!-- Encoders: publish sensor_msgs/Range (value in degrees) to the host -->
  <encoders>
    <encoder port="E1" topic="encoders/e1"/>
    <encoder port="E2" topic="encoders/e2"/>
    <encoder port="E3" topic="encoders/e3"/>
    <encoder port="E4" topic="encoders/e4"/>
  </encoders>

  <!-- Motors: subscribe to std_msgs/Int32 from the host -->
  <motors>
    <motor port="M1" topic="motors/m1"/>
    <motor port="M2" topic="motors/m2"/>
    <motor port="M3" topic="motors/m3"/>
    <motor port="M4" topic="motors/m4"/>
  </motors>

  <!-- Servos: subscribe to std_msgs/Int32 from the host -->
  <servos>
    <servo port="S1" topic="servos/s1"/>
    <servo port="S2" topic="servos/s2"/>
    <servo port="S3" topic="servos/s3"/>
    <servo port="S4" topic="servos/s4"/>
    <servo port="S5" topic="servos/s5"/>
    <servo port="S6" topic="servos/s6"/>
  </servos>

  <!-- IMU: publish geometry_msgs/Vector3Stamped to the host. -->
  <imu>
    <accelerometer topic="imu/accelerometer"/>
    <gyroscope     topic="imu/gyroscope"/>
  </imu>

</ros2config>
```

### The `rate_hz` parameter — what it means in ROS 2 terms

`rate_hz` is the frequency of the node's internal `wall timer`
(`rclcpp::create_wall_timer`), which **polls all sensors, encoders, the
accelerometer and the gyroscope once every `1/rate_hz` seconds and publishes
one message to each of their topics**. In other words, this is the **publish
rate for all publishers at once** — it is shared across the whole node, and
cannot be set separately per port.

**Important:** `rate_hz` does not affect motors and servos. Command
subscribers (`motors/*`, `servos/*`) react to every incoming message
immediately, in an event-driven fashion, not on a timer — see the
[subscribers section](#topics-publishers-and-subscribers).

### Disabling a sensor/port you don't need

If some port (sensor, encoder, motor, servo, accelerometer, gyroscope) is not
used in the current task, comment out the corresponding XML line with an XML
comment `<!-- ... -->`, for example:

```xml
<sensors>
  <sensor port="A1" topic="sensors/a1"/>
  <!-- <sensor port="A2" topic="sensors/a2"/> -->
  <sensor port="A3" topic="sensors/a3"/>
  ...
</sensors>
```

You can also simply delete the line — the result is the same. After editing
the file, the node must be **restarted** (stop `libtrikRos2.so.1` and start it
again) for the configuration changes to take effect.

You can also rename the `topic` for a port — for example, if the host already
uses different topic names — just change the `topic="..."` attribute on the
relevant element.

## Topics: publishers and subscribers

### Publishers — data flows from the controller to the host

Published once every `1/rate_hz` seconds, with a separate topic for each
configured port.

| Data source | Default topic | Message type | What's in the topic |
|---|---|---|---|
| Analog sensor A1-A6 | `sensors/aN` | `sensor_msgs/msg/Range` | sensor distance/reading |
| Digital sensor D1-D3 | `sensors/dN` | `sensor_msgs/msg/Range` | sensor reading |
| Encoder E1-E4 | `encoders/eN` | `sensor_msgs/msg/Range` | shaft angle in degrees (packed into the `range` field) |
| Accelerometer | `imu/accelerometer` | `geometry_msgs/msg/Vector3Stamped` | X/Y/Z readings |
| Gyroscope | `imu/gyroscope` | `geometry_msgs/msg/Vector3Stamped` | X/Y/Z readings |

### Subscribers — commands flow from the host to the controller

React to every received message immediately (event-driven), queue depth of 1
— only the last received command matters.

| Command target | Default topic | Message type | What to send |
|---|---|---|---|
| Motor M1-M4 | `motors/mN` | `std_msgs/msg/Int32` | power, integer |
| Servo S1-S6 | `servos/sN` | `std_msgs/msg/Int32` | power/angle, integer |

## Message formats

### `sensor_msgs/msg/Range` — sensors A1-A6, D1-D3

```
std_msgs/Header header
  builtin_interfaces/Time stamp   # publication time on the controller
  string frame_id                # not filled in, always "" (empty string)
uint8 radiation_type              # always INFRARED (85) — see note below
float32 field_of_view             # 0.1 rad, fixed value
float32 min_range                 # minimum sensor reading, meters
float32 max_range                 # maximum sensor reading, meters
float32 range                     # current sensor reading, meters
```

How `range`, `min_range`, `max_range` are computed:

- the controller reads the raw integer sensor value (`SensorInterface::read()`,
  units — centimeters for range finders) and converts it to meters by
  dividing by 100: `range = read() * 0.01`;
- `min_range`/`max_range` are taken from the sensor's `minValue()`/`maxValue()`
  (bounds defined in the robot model configuration) and are likewise converted
  to meters.

**Note:** the `radiation_type` field is always `INFRARED`, and `field_of_view`
is always `0.1` rad — these are fixed values that do not depend on the actual
sensor type (A5/A6, for example, often have light sensors rather than range
finders). The `sensor_msgs/Range` message is reused as a convenient standard
container for "value + time + range" — read the `range` field first and, if
needed, `min_range`/`max_range` to determine the reading range of a specific
port. If you need the raw (not converted to meters) value — multiply `range`
by 100.

### `sensor_msgs/msg/Range` — encoders E1-E4

The same message type is reused for encoders, but the field semantics differ:

```
std_msgs/Header header
  builtin_interfaces/Time stamp
  string frame_id                # not filled in, always ""
uint8 radiation_type              # always INFRARED, carries no meaning
float32 field_of_view             # always 0.0, carries no meaning
float32 min_range                 # always 0.0, carries no meaning
float32 max_range                 # always 0.0, carries no meaning
float32 range                     # encoder shaft angle, DEGREES (not meters!)
```

`range` is `EncoderInterface::read()` (angle in degrees), cast to `float`,
without any scaling. This is a cumulative motor tick counter converted to
degrees (see `Encoder::read()` in `trikControl`) — it grows/decreases without
bound and is only reset by an explicit `reset()` call, so it has no physical
minimum/maximum. The `min_range`/`max_range` fields carry no information
(always `0.0`) — read only `range`. On the host, cast `range` back to `int` if
you need a whole number of degrees:

```python
degrees = int(round(msg.range))
```

### `geometry_msgs/msg/Vector3Stamped` — accelerometer and gyroscope

```
std_msgs/Header header
  builtin_interfaces/Time stamp   # publication time on the controller
  string frame_id                 # not filled in, always ""
geometry_msgs/Vector3 vector
  float64 x
  float64 y
  float64 z
```

`x`, `y`, `z` are the **integer readings** of the accelerometer or gyroscope
sensors (values from `VectorSensorInterface::read()`, cast to `double`) along
the X, Y, Z axes.

### `std_msgs/msg/Int32` — motors and servos (commands)

```
int32 data
```

It is enough for the host to publish a single message with the `data` field —
the controller immediately calls `setPower(data)` on the corresponding
motor/servo. Out-of-range values are constrained by the motor driver itself.

## Default port table

If the configuration file is not found/is corrupted, the built-in "everything
enabled" port set is used:

| Category | Ports | Topics | rate_hz |
|---|---|---|---|
| Sensors (analog) | A1, A2, A3, A4, A5, A6 | `sensors/a1` … `sensors/a6` | 20.0 |
| Sensors (digital) | D1, D2, D3 | `sensors/d1` … `sensors/d3` | 20.0 |
| Encoders | E1, E2, E3, E4 | `encoders/e1` … `encoders/e4` | 20.0 |
| Motors | M1, M2, M3, M4 | `motors/m1` … `motors/m4` | — (event-driven) |
| Servos | S1 … S6 | `servos/s1` … `servos/s6` | — (event-driven) |
| IMU | accelerometer, gyroscope | `imu/accelerometer`, `imu/gyroscope` | 20.0 |

Check the actual contents of `/home/root/trik/ros2_config.xml` on your
controller.

## Host ⇄ controller networking

Since CycloneDDS is used, for the nodes to discover each other you need:

1. **The same `ROS_DOMAIN_ID`** on the host and the controller (`0` on both
   sides by default, unless changed explicitly):
   ```bash
   export ROS_DOMAIN_ID=0
   ```
2. **The same RMW** — on the host you must set:
   ```bash
   export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
   ```
3. **Working multicast/discovery** between the host and the controller. If
   both devices are on the same Wi-Fi/Ethernet network with working multicast
   (e.g. both connected to the robot's access point) — discovery usually
   happens automatically.

   If multicast doesn't work (different subnets, a Docker container on the
   host without `--network host`, some Wi-Fi routers block multicast between
   clients) — you need to explicitly specify the TRIK controller's CycloneDDS
   address via a configuration file on the host side:

   ```xml
   <!-- cyclonedds.xml on the host -->
   <CycloneDDS>
     <Domain>
       <Discovery>
         <Peers>
           <!-- IP address of the TRIK controller on the local network -->
           <Peer address="192.168.1.100"/>
         </Peers>
       </Discovery>
     </Domain>
   </CycloneDDS>
   ```

   and pass the path to it via an environment variable before starting the
   host's node:
   ```bash
   export CYCLONEDDS_URI="file:///path/to/cyclonedds.xml"
   ```

   The controller's IP address can be found on the controller itself, in the
   main interface menu.

4. If the host runs in Docker — the container must use `--network host`
   (or explicitly forward the DDS UDP ports), otherwise multicast/DDS traffic
   will not reach the controller.

## Host-side code examples

This documentation only describes the controller side — the host-side code is
written by the user in any language ROS 2 supports (C++, Python, etc.). Below
are minimal illustrative examples of how to subscribe to a sensor and how to
send a command to a motor, to demonstrate the use of the topics and message
types described above.

### Checking topics without writing code

```bash
# what the controller is publishing right now
ros2 topic echo /sensors/a1
ros2 topic echo /encoders/e1
ros2 topic echo /imu/accelerometer

# send motor M1 a "power 50" command
ros2 topic pub --once /motors/m1 std_msgs/msg/Int32 "{data: 50}"

# stop the motor
ros2 topic pub --once /motors/m1 std_msgs/msg/Int32 "{data: 0}"
```

### C++ (rclcpp)

```cpp
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/range.hpp>
#include <std_msgs/msg/int32.hpp>

class TrikExample : public rclcpp::Node {
public:
    TrikExample() : Node("trik_example") {
        motor_m1_ = create_publisher<std_msgs::msg::Int32>("motors/m1", 1);

        sensor_a1_ = create_subscription<sensor_msgs::msg::Range>(
            "sensors/a1", 10,
            [this](sensor_msgs::msg::Range::SharedPtr msg) {
                RCLCPP_INFO(get_logger(), "A1 = %.3f m", msg->range);

                std_msgs::msg::Int32 cmd;
                cmd.data = (msg->range < 0.10) ? 0 : 40;
                motor_m1_->publish(cmd);
            });
    }

private:
    rclcpp::Subscription<sensor_msgs::msg::Range>::SharedPtr sensor_a1_;
    rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr motor_m1_;
};

int main(int argc, char ** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<TrikExample>());
    rclcpp::shutdown();
    return 0;
}
```
