/**
 * @file main.cpp
 * @brief 0x0001 比赛状态收发最小验证
 *
 * UART8 每 100 ms 发送一次 0x0001 帧，UART9 由 referee 模块异步接收并发布到
 * referee_game_state_chan；订阅者把字段以 JustFloat 格式写到 RTT 通道 1。
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <SEGGER_RTT.h>

#include <breeze/referee/channels.h>
#include <breeze/referee/messages.h>
#include <breeze/referee/module.h>

#include <etl/optional.h>
#include <etl/span.h>
#include <robotpilots/algorithm/crc.hpp>

#include <cstring>

LOG_MODULE_REGISTER(game_status_test, LOG_LEVEL_INF);

namespace
{

constexpr uint8_t sof = 0xa5;
constexpr uint16_t game_status_id = 0x0001;
constexpr size_t header_size = 5;
constexpr size_t cmd_size = 2;
constexpr size_t payload_size = 11;
constexpr size_t tail_size = 2;
constexpr size_t frame_size = header_size + cmd_size + payload_size + tail_size;

struct __attribute__((packed)) game_status_payload {
	uint8_t game_type: 4;
	uint8_t game_progress: 4;
	uint16_t stage_remain_time;
	uint64_t sync_time_stamp;
};
static_assert(sizeof(game_status_payload) == payload_size, "0x0001 payload 长度不符");

const device *const tx_uart = DEVICE_DT_GET(DT_NODELABEL(uart8));
const device *const rx_uart = DEVICE_DT_GET(DT_NODELABEL(uart9));

K_THREAD_STACK_DEFINE(tx_stack, 1024);
k_thread tx_thread;
K_THREAD_STACK_DEFINE(vofa_stack, 1024);
k_thread vofa_thread;

constexpr size_t vofa_channel = 1;
uint8_t vofa_buffer[2048];
constexpr uint8_t vofa_tail[4] = {0x00, 0x00, 0x80, 0x7f};

struct vofa_state {
	uint8_t game_type;
	uint8_t game_progress;
	uint16_t stage_remain_time;
};

K_MUTEX_DEFINE(vofa_state_mutex);
vofa_state latest_vofa_state{};

void vofa_write(const float *data, size_t count)
{
	SEGGER_RTT_Write(vofa_channel, data, count * sizeof(float));
	SEGGER_RTT_Write(vofa_channel, vofa_tail, sizeof(vofa_tail));
}

void on_game_state(const zbus_channel *chan)
{
	const auto *msg =
		static_cast<const breeze::referee::game_state_info *>(zbus_chan_const_msg(chan));

	k_mutex_lock(&vofa_state_mutex, K_FOREVER);
	latest_vofa_state = {
		msg->game_type,
		msg->game_progress,
		msg->stage_remain_time,
	};
	k_mutex_unlock(&vofa_state_mutex);
}

ZBUS_LISTENER_DEFINE(game_state_listener, on_game_state);
ZBUS_CHAN_ADD_OBS(referee_game_state_chan, game_state_listener, 1);

void send_game_status(uint8_t seq, uint8_t game_type, uint8_t game_progress, uint16_t remain)
{
	uint8_t frame[frame_size]{};

	frame[0] = sof;
	frame[1] = static_cast<uint8_t>(payload_size);
	frame[2] = 0;
	frame[3] = seq;
	frame[4] = robotpilots::algorithm::Crc8Calculate(
		etl::span<const uint8_t>(frame, header_size - 1));

	memcpy(&frame[header_size], &game_status_id, cmd_size);

	game_status_payload payload{};
	payload.game_type = game_type;
	payload.game_progress = game_progress;
	payload.stage_remain_time = remain;
	memcpy(&frame[header_size + cmd_size], &payload, sizeof(payload));

	const uint16_t tail = robotpilots::algorithm::Crc16Calculate(
		etl::span<const uint8_t>(frame, frame_size - tail_size));
	memcpy(&frame[frame_size - tail_size], &tail, sizeof(tail));

	for (const auto byte : frame) {
		uart_poll_out(tx_uart, byte);
	}
}

void tx_entry(void *, void *, void *)
{
	uint8_t seq = 0;
	while (true) {
		const auto elapsed_seconds = k_uptime_get_32() / 1000U;
		const auto game_progress = static_cast<uint8_t>((elapsed_seconds / 30U) % 6U);
		const auto remain = static_cast<uint16_t>(300U - (elapsed_seconds % 300U));

		send_game_status(seq++, 1, game_progress, remain);
		k_sleep(K_MSEC(100));
	}
}

void vofa_entry(void *, void *, void *)
{
	while (true) {
		vofa_state state;
		k_mutex_lock(&vofa_state_mutex, K_FOREVER);
		state = latest_vofa_state;
		k_mutex_unlock(&vofa_state_mutex);

		const float frame[4] = {
			static_cast<float>(k_uptime_get_32()) / 1000.0f,
			static_cast<float>(state.game_type),
			static_cast<float>(state.game_progress),
			static_cast<float>(state.stage_remain_time),
		};
		vofa_write(frame, 4);
		k_sleep(K_MSEC(100));
	}
}

} // namespace

int main()
{
	SEGGER_RTT_ConfigUpBuffer(vofa_channel, "vofa", vofa_buffer, sizeof(vofa_buffer),
				  SEGGER_RTT_MODE_NO_BLOCK_SKIP);

	if (!device_is_ready(tx_uart) || !device_is_ready(rx_uart)) {
		LOG_ERR("uart8/uart9 not ready");
		return 0;
	}

	if (!breeze::referee::init(etl::optional<const device *>(rx_uart))) {
		LOG_ERR("referee init failed");
		return 0;
	}

	k_thread_create(&tx_thread, tx_stack, K_THREAD_STACK_SIZEOF(tx_stack), tx_entry, nullptr,
			nullptr, nullptr, 7, 0, K_NO_WAIT);
	k_thread_create(&vofa_thread, vofa_stack, K_THREAD_STACK_SIZEOF(vofa_stack), vofa_entry,
			nullptr, nullptr, nullptr, 7, 0, K_NO_WAIT);

	LOG_INF("game_status 0x0001 test: uart8 -> uart9, vofa on RTT ch1");
	return 0;
}
