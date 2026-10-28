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

constexpr uint8_t SOF = 0xa5;
constexpr uint16_t GAME_STATUS_ID = 0x0001;
constexpr size_t HEADER_SIZE = 5;
constexpr size_t CMD_SIZE = 2;
constexpr size_t PAYLOAD_SIZE = 11;
constexpr size_t TAIL_SIZE = 2;
constexpr size_t FRAME_SIZE = HEADER_SIZE + CMD_SIZE + PAYLOAD_SIZE + TAIL_SIZE;

struct __attribute__((packed)) GameStatusPayload {
	uint8_t GameType: 4;
	uint8_t GameProgress: 4;
	uint16_t StageRemainTime;
	uint64_t SyncTimeStamp;
};
static_assert(sizeof(GameStatusPayload) == PAYLOAD_SIZE, "0x0001 payload 长度不符");

const device *const txUart = DEVICE_DT_GET(DT_NODELABEL(uart8));
const device *const rxUart = DEVICE_DT_GET(DT_NODELABEL(uart9));

K_THREAD_STACK_DEFINE(tx_stack, 1024);
k_thread tx_thread;
K_THREAD_STACK_DEFINE(vofa_stack, 1024);
k_thread vofa_thread;

constexpr size_t VOFA_CHANNEL = 1;
uint8_t vofa_buffer[2048];
constexpr uint8_t VOFA_TAIL[4] = {0x00, 0x00, 0x80, 0x7f};

struct VofaState {
	uint8_t GameType;
	uint8_t GameProgress;
	uint16_t StageRemainTime;
};

K_MUTEX_DEFINE(vofa_state_mutex);
VofaState latest_vofa_state{};

void VofaWrite(const float *data, size_t count)
{
	SEGGER_RTT_Write(VOFA_CHANNEL, data, count * sizeof(float));
	SEGGER_RTT_Write(VOFA_CHANNEL, VOFA_TAIL, sizeof(VOFA_TAIL));
}

void OnGameState(const zbus_channel *chan)
{
	const auto *msg =
		static_cast<const breeze::referee::GameStateInfo *>(zbus_chan_const_msg(chan));

	k_mutex_lock(&vofa_state_mutex, K_FOREVER);
	latest_vofa_state = {
		msg->GameType,
		msg->GameProgress,
		msg->StageRemainTime,
	};
	k_mutex_unlock(&vofa_state_mutex);
}

ZBUS_LISTENER_DEFINE(gameStateListener, OnGameState);
ZBUS_CHAN_ADD_OBS(refereeGameStateChan, gameStateListener, 1);

void SendGameStatus(uint8_t seq, uint8_t gameType, uint8_t gameProgress, uint16_t remain)
{
	uint8_t frame[FRAME_SIZE]{};

	frame[0] = SOF;
	frame[1] = static_cast<uint8_t>(PAYLOAD_SIZE);
	frame[2] = 0;
	frame[3] = seq;
	frame[4] = robotpilots::algorithm::Crc8Calculate(
		etl::span<const uint8_t>(frame, HEADER_SIZE - 1));

	memcpy(&frame[HEADER_SIZE], &GAME_STATUS_ID, CMD_SIZE);

	GameStatusPayload payload{};
	payload.GameType = gameType;
	payload.GameProgress = gameProgress;
	payload.StageRemainTime = remain;
	memcpy(&frame[HEADER_SIZE + CMD_SIZE], &payload, sizeof(payload));

	const uint16_t tail = robotpilots::algorithm::Crc16Calculate(
		etl::span<const uint8_t>(frame, FRAME_SIZE - TAIL_SIZE));
	memcpy(&frame[FRAME_SIZE - TAIL_SIZE], &tail, sizeof(tail));

	for (const auto byte : frame) {
		uart_poll_out(txUart, byte);
	}
}

void TxEntry(void *, void *, void *)
{
	uint8_t seq = 0;
	while (true) {
		const auto elapsedSeconds = k_uptime_get_32() / 1000U;
		const auto gameProgress = static_cast<uint8_t>((elapsedSeconds / 30U) % 6U);
		const auto remain = static_cast<uint16_t>(300U - (elapsedSeconds % 300U));

		SendGameStatus(seq++, 1, gameProgress, remain);
		k_sleep(K_MSEC(100));
	}
}

void VofaEntry(void *, void *, void *)
{
	while (true) {
		VofaState state;
		k_mutex_lock(&vofa_state_mutex, K_FOREVER);
		state = latest_vofa_state;
		k_mutex_unlock(&vofa_state_mutex);

		const float frame[4] = {
			static_cast<float>(k_uptime_get_32()) / 1000.0f,
			static_cast<float>(state.GameType),
			static_cast<float>(state.GameProgress),
			static_cast<float>(state.StageRemainTime),
		};
		VofaWrite(frame, 4);
		k_sleep(K_MSEC(100));
	}
}

} // namespace

int main()
{
	SEGGER_RTT_ConfigUpBuffer(VOFA_CHANNEL, "vofa", vofa_buffer, sizeof(vofa_buffer),
				  SEGGER_RTT_MODE_NO_BLOCK_SKIP);

	if (!device_is_ready(txUart) || !device_is_ready(rxUart)) {
		LOG_ERR("uart8/uart9 not ready");
		return 0;
	}

	if (!breeze::referee::Init(etl::optional<const device *>(rxUart))) {
		LOG_ERR("referee init failed");
		return 0;
	}

	k_thread_create(&tx_thread, tx_stack, K_THREAD_STACK_SIZEOF(tx_stack), TxEntry, nullptr,
			nullptr, nullptr, 7, 0, K_NO_WAIT);
	k_thread_create(&vofa_thread, vofa_stack, K_THREAD_STACK_SIZEOF(vofa_stack), VofaEntry,
			nullptr, nullptr, nullptr, 7, 0, K_NO_WAIT);

	LOG_INF("game_status 0x0001 test: uart8 -> uart9, vofa on RTT ch1");
	return 0;
}
