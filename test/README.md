# Test Cases for uC/OS-II ARMv8 Demo

This directory contains automated test cases for validating the uC/OS-II port on ARMv8-A architecture.

## Test Case 1: Context Switch and Timer Validation

**File:** `test_context_timer.c`

**Purpose:** Verify that uC/OS-II task context switching and timer interrupts work correctly.

**Test Behavior:**
- Creates two tasks (Task A and Task B)
- Task A prints a counter every second using `OSTimeDlyHMSM()`
- Task B executes faster (500ms interval) to test preemptive multitasking
- Runs for 8 seconds and collects statistics
- Reports task switch counts and timer tick counts

**Success Criteria:**
- Both tasks execute at least 3 times
- OS timer ticks accumulate (expected >= 80 ticks for 8 seconds)
- No crashes or hangs during execution
- Context switching occurs correctly between tasks

**Run Command:**
```bash
make test-timer
```

**Expected Output:**
```
========================================
TEST CASE 1: RESULTS
========================================
Task A switches: 8
Task B switches: 16
OS Timer ticks: 8000

[PASS] ✓ Context switch and timer test PASSED
========================================
```

---

## Test Case 2: Network TAP Ping Test with Response Time Reporting

**File:** `test_network_ping.c`

**Purpose:** Verify VirtIO-net driver functionality with TAP interface and measure network performance.

**Test Behavior:**
1. Initializes VirtIO-net driver
2. Performs ARP resolution to discover peer MAC address
3. Sends 5 ICMP echo requests (pings) to peer
4. Measures and reports response times for each ping
5. Calculates min/max/average response times and packet loss

**Success Criteria:**
- Driver initialization succeeds
- ARP resolution completes successfully
- At least 3 ping responses received
- Average response time < 100ms
- No unexpected packet loss

**Prerequisites:**
- TAP interface `qemu-lan` must be configured
- Peer must have IP address 192.168.1.103
- Host networking must allow ICMP packets

**Setup TAP Interface (Linux):**
```bash
# Create TAP interface
sudo ip tuntap add dev qemu-lan mode tap user $USER

# Configure IP address
sudo ip addr add 192.168.1.103/24 dev qemu-lan

# Bring up interface
sudo ip link set qemu-lan up

# Verify
ip addr show qemu-lan
```

**Run Command:**
```bash
make test-ping
```

**Expected Output:**
```
========================================
TEST CASE 2: RESULTS
========================================
Network Configuration:
  Local IP:  192.168.1.1
  Peer IP:   192.168.1.103
  ARP Status: Resolved

Ping Statistics:
  Sent:     5
  Received: 5
  Loss:     0%

Response Times:
  Min:      2 ms
  Max:      8 ms
  Average:  4 ms

[PASS] ✓ Network ping test PASSED
========================================
```

---

## Running All Tests

To run both test cases sequentially:

```bash
make test-all
```

---

## Test Results Interpretation

### Test Case 1 - Pass Conditions:
- ✓ Task A switches >= 3
- ✓ Task B switches >= 3
- ✓ OS timer ticks >= (test_duration * 10)

### Test Case 2 - Pass Conditions:
- ✓ ARP resolution successful
- ✓ Ping responses >= 3
- ✓ Average response time < 100ms

### Troubleshooting

**Test Case 1 Fails:**
- Check GICv3 initialization
- Verify timer interrupt handler registration
- Check DAIF register (IRQs must be enabled)
- Review context switch assembly code

**Test Case 2 Fails:**

*ARP Resolution Timeout:*
- Verify TAP interface is up: `ip link show qemu-lan`
- Check IP address: `ip addr show qemu-lan`
- Ensure firewall allows ARP packets

*No Ping Responses:*
- Verify ICMP is enabled on host: `sudo sysctl net.ipv4.icmp_echo_ignore_all`
- Check firewall rules: `sudo iptables -L`
- Verify VirtIO-net interrupt handling
- Check packet transmission in driver

*High Response Times:*
- Review host system load
- Check for kernel message rate limiting
- Verify virtqueue processing efficiency

---

## Directory Structure

```
test/
├── README.md                    # This file
├── test_context_timer.c         # Test Case 1: Context Switch & Timer
└── test_network_ping.c          # Test Case 2: Network Ping Test
```

---

## Adding New Tests

When adding new test cases:

1. Create test source file in `test/` directory
2. Follow the naming convention: `test_<feature>.c`
3. Include comprehensive pass/fail reporting
4. Update `Makefile` with new test target
5. Document prerequisites and expected results in this README
6. Ensure test is self-contained and automated

---

## Notes

- Tests are designed to be self-validating
- Each test reports clear PASS/FAIL status
- Tests use a timeout to prevent infinite hangs
- All tests require QEMU with ARMv8 support
- Test Case 2 requires host network configuration
