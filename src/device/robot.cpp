#include "robot.hpp"

#include "spdlog/spdlog.h"

void Robot::ThreadRecv() {
  SPDLOG_DEBUG("[ThreadRecv] Started.");

  uint16_t id;
  Protocol_Referee_t ref;
  Protocol_MCU_t robot;

  while (thread_continue) {
    serial_.Recv(&id, sizeof(id));

    if (AI_ID_REF == id) {
      serial_.Recv(&ref, sizeof(ref));

      if (crc16::CRC16_Verify((uint8_t *)&ref_, sizeof(ref_))) {
        mutex_ref_.lock();
        std::memcpy(&ref_, &(ref.data), sizeof(ref_));
        mutex_ref_.unlock();
      }
    } else if (AI_ID_MCU == id) {
      serial_.Recv(&robot, sizeof(robot));

      if (crc16::CRC16_Verify((uint8_t *)&mcu_, sizeof(mcu_))) {
        mutex_mcu_.lock();
        std::memcpy(&mcu_, &(robot.data), sizeof(mcu_));
        mutex_mcu_.unlock();
      }
    }
  }
  SPDLOG_DEBUG("[ThreadRecv] Stoped.");
}

void Robot::ThreadTrans() {
  SPDLOG_DEBUG("[ThreadTrans] Started.");

  Protocol_AI_t command;

  while (thread_continue) {
    mutex_commandq_.lock();
    if (!commandq_.empty()) {
      command.data = commandq_.front();
      command.crc16 = crc16::CRC16_Calc((uint8_t *)&command.data,
                                        sizeof(command.data), UINT16_MAX);
      serial_.Trans((char *)&command, sizeof(command));
      commandq_.pop();
    }
    mutex_commandq_.unlock();
  }
  SPDLOG_DEBUG("[ThreadTrans] Stoped.");
}

Robot::Robot(const std::string &dev_path) {
  serial_.Open(dev_path);
  serial_.Config();
  if (!serial_.IsOpen()) {
    SPDLOG_ERROR("Can't open device.");
  }

  thread_continue = true;
  thread_recv_ = std::thread(&Robot::ThreadRecv, this);
  thread_trans_ = std::thread(&Robot::ThreadTrans, this);

  SPDLOG_TRACE("Constructed.");
}

Robot::~Robot() {
  serial_.Close();

  thread_continue = false;
  thread_recv_.join();
  thread_trans_.join();
  SPDLOG_TRACE("Destructed.");
}