/* Copyright (c) 2018-2019, Stanford University
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <gtest/gtest.h>

#include "Receiver.h"

#include "Mock/MockDriver.h"
#include "Mock/MockScheduler.h"
#include "OpContext.h"

#include <Homa/Debug.h>

#include <mutex>

namespace Homa {
namespace Core {
namespace {

using ::testing::Eq;
using ::testing::Matcher;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Return;

class ReceiverTest : public ::testing::Test {
  public:
    ReceiverTest()
        : mockDriver()
        , mockPacket(&payload)
        , mockScheduler(&mockDriver)
        , payload()
        , receiver()
        , savedLogPolicy(Debug::getLogPolicy())
    {
        ON_CALL(mockDriver, getBandwidth).WillByDefault(Return(8000));
        ON_CALL(mockDriver, getMaxPayloadSize).WillByDefault(Return(1028));
        Debug::setLogPolicy(
            Debug::logPolicyFromString("src/ObjectPool@SILENT"));
        receiver = new Receiver(&mockScheduler);
    }

    ~ReceiverTest()
    {
        delete receiver;
        Debug::setLogPolicy(savedLogPolicy);
    }

    NiceMock<Homa::Mock::MockDriver> mockDriver;
    NiceMock<Homa::Mock::MockDriver::MockPacket> mockPacket;
    NiceMock<Homa::Mock::MockScheduler> mockScheduler;
    char payload[1028];
    Receiver* receiver;
    std::vector<std::pair<std::string, std::string>> savedLogPolicy;
};

TEST_F(ReceiverTest, handleDataPacket_basic)
{
    // Setup registered op
    OpContext op_(nullptr, &mockDriver);
    OpContext* op = &op_;
    Protocol::MessageId id(42, 32, 22);
    Receiver::InboundMessage* message = receiver->messagePool.construct();
    message->id = id;
    op->inMessage = message;
    receiver->registeredOps.insert({id, op});

    EXPECT_TRUE(receiver->unregisteredMessages.empty());
    EXPECT_TRUE(receiver->receivedMessages.empty());

    OpContext* ret = nullptr;

    // receive packet 1
    Protocol::Packet::DataHeader* header =
        static_cast<Protocol::Packet::DataHeader*>(mockPacket.payload);
    header->common.messageId = id;
    header->index = 1;
    header->totalLength = 1420;
    std::string addressStr("remote-location");
    Homa::Mock::MockDriver::MockAddress mockAddress;
    mockPacket.address = &mockAddress;

    EXPECT_CALL(mockDriver, releasePackets(Pointee(&mockPacket), Eq(1)))
        .Times(0);
    EXPECT_CALL(mockAddress, toString)
        .Times(3)
        .WillRepeatedly(Return(addressStr))
        .RetiresOnSaturation();
    EXPECT_CALL(mockDriver,
                getAddress(Matcher<std::string const*>(Pointee(addressStr))))
        .WillOnce(Return(&mockAddress));
    EXPECT_CALL(mockScheduler,
                packetReceived(Eq(id), Eq(&mockAddress),
                               Eq(header->totalLength), Eq(1000)))
        .Times(1);

    ret = receiver->handleDataPacket(&mockPacket, &mockDriver);

    EXPECT_TRUE(receiver->unregisteredMessages.empty());
    EXPECT_TRUE(receiver->receivedMessages.empty());
    EXPECT_TRUE(op->inMessage.load()->message);
    EXPECT_EQ(&mockAddress, op->inMessage.load()->source);
    EXPECT_EQ(1420U, op->inMessage.load()->message->messageLength);
    EXPECT_TRUE(op->inMessage.load()->message->occupied.test(1));
    EXPECT_EQ(1U, op->inMessage.load()->message->getNumPackets());
    EXPECT_EQ(1000U, op->inMessage.load()->message->PACKET_DATA_LENGTH);
    EXPECT_FALSE(op->inMessage.load()->fullMessageReceived);
    EXPECT_EQ(nullptr, ret);

    Mock::VerifyAndClearExpectations(&mockDriver);
    Mock::VerifyAndClearExpectations(&mockAddress);
    Mock::VerifyAndClearExpectations(&mockScheduler);

    // receive packet 1 again; duplicate packet

    EXPECT_CALL(mockDriver, releasePackets(Pointee(&mockPacket), Eq(1)))
        .Times(1);
    EXPECT_CALL(mockDriver,
                getAddress(Matcher<std::string const*>(Pointee(addressStr))))
        .Times(0);
    EXPECT_CALL(mockAddress, toString)
        .Times(2)
        .WillRepeatedly(Return(addressStr))
        .RetiresOnSaturation();
    EXPECT_CALL(mockScheduler, packetReceived).Times(0);

    ret = receiver->handleDataPacket(&mockPacket, &mockDriver);

    EXPECT_TRUE(receiver->unregisteredMessages.empty());
    EXPECT_TRUE(receiver->receivedMessages.empty());
    EXPECT_TRUE(op->inMessage.load()->message->occupied.test(1));
    EXPECT_EQ(1U, op->inMessage.load()->message->getNumPackets());
    EXPECT_EQ(1000U, op->inMessage.load()->message->PACKET_DATA_LENGTH);
    EXPECT_FALSE(op->inMessage.load()->fullMessageReceived);
    EXPECT_EQ(nullptr, ret);

    Mock::VerifyAndClearExpectations(&mockDriver);
    Mock::VerifyAndClearExpectations(&mockAddress);
    Mock::VerifyAndClearExpectations(&mockScheduler);

    // receive packet 0; complete the message
    header->index = 0;

    EXPECT_CALL(mockDriver, releasePackets(Pointee(&mockPacket), Eq(1)))
        .Times(0);
    EXPECT_CALL(mockDriver,
                getAddress(Matcher<std::string const*>(Pointee(addressStr))))
        .Times(0);
    EXPECT_CALL(mockAddress, toString)
        .Times(2)
        .WillRepeatedly(Return(addressStr))
        .RetiresOnSaturation();
    EXPECT_CALL(mockScheduler,
                packetReceived(Eq(id), Eq(&mockAddress),
                               Eq(header->totalLength), Eq(2000)))
        .Times(1);

    ret = receiver->handleDataPacket(&mockPacket, &mockDriver);

    EXPECT_TRUE(receiver->unregisteredMessages.empty());
    EXPECT_TRUE(receiver->receivedMessages.empty());
    EXPECT_TRUE(op->inMessage.load()->message->occupied.test(0));
    EXPECT_EQ(2U, op->inMessage.load()->message->getNumPackets());
    EXPECT_EQ(1000U, op->inMessage.load()->message->PACKET_DATA_LENGTH);
    EXPECT_TRUE(op->inMessage.load()->fullMessageReceived);
    EXPECT_EQ(op, ret);

    Mock::VerifyAndClearExpectations(&mockDriver);
    Mock::VerifyAndClearExpectations(&mockAddress);
    Mock::VerifyAndClearExpectations(&mockScheduler);

    // receive packet 0 again on a complete message
    EXPECT_CALL(mockDriver, releasePackets(Pointee(&mockPacket), Eq(1)))
        .Times(1);
    EXPECT_CALL(mockDriver,
                getAddress(Matcher<std::string const*>(Pointee(addressStr))))
        .Times(0);
    EXPECT_CALL(mockAddress, toString).Times(0);
    EXPECT_CALL(mockScheduler, packetReceived).Times(0);

    ret = receiver->handleDataPacket(&mockPacket, &mockDriver);

    EXPECT_TRUE(receiver->unregisteredMessages.empty());
    EXPECT_TRUE(receiver->receivedMessages.empty());
    EXPECT_EQ(nullptr, ret);
    Mock::VerifyAndClearExpectations(&mockDriver);
}

TEST_F(ReceiverTest, handleDataPacket_existingUnregistered)
{
    Protocol::MessageId id(42, 32, 22);
    Protocol::Packet::DataHeader* header =
        static_cast<Protocol::Packet::DataHeader*>(mockPacket.payload);
    header->common.messageId = id;
    header->index = 1;
    header->totalLength = 1420;
    std::string addressStr("remote-location");
    Homa::Mock::MockDriver::MockAddress mockAddress;
    mockPacket.address = &mockAddress;

    Receiver::InboundMessage* message = receiver->messagePool.construct();
    message->id = id;
    receiver->unregisteredMessages.insert({id, message});
    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(1U, receiver->unregisteredMessages.size());
    EXPECT_TRUE(receiver->receivedMessages.empty());

    EXPECT_CALL(mockAddress, toString)
        .Times(3)
        .WillRepeatedly(Return(addressStr))
        .RetiresOnSaturation();
    EXPECT_CALL(mockDriver,
                getAddress(Matcher<std::string const*>(Pointee(addressStr))))
        .WillOnce(Return(&mockAddress));

    receiver->handleDataPacket(&mockPacket, &mockDriver);

    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(1U, receiver->unregisteredMessages.size());
    EXPECT_EQ(message, receiver->unregisteredMessages.find(id)->second);
    EXPECT_TRUE(receiver->receivedMessages.empty());
}

TEST_F(ReceiverTest, handleDataPacket_newUnregistered)
{
    Protocol::MessageId id(42, 32, 22);
    Protocol::Packet::DataHeader* header =
        static_cast<Protocol::Packet::DataHeader*>(mockPacket.payload);
    header->common.messageId = id;
    header->index = 1;
    header->totalLength = 1420;
    std::string addressStr("remote-location");
    Homa::Mock::MockDriver::MockAddress mockAddress;
    mockPacket.address = &mockAddress;

    EXPECT_EQ(0U, receiver->messagePool.outstandingObjects);
    EXPECT_TRUE(receiver->unregisteredMessages.empty());
    EXPECT_TRUE(receiver->receivedMessages.empty());

    EXPECT_CALL(mockAddress, toString)
        .Times(3)
        .WillRepeatedly(Return(addressStr))
        .RetiresOnSaturation();
    EXPECT_CALL(mockDriver,
                getAddress(Matcher<std::string const*>(Pointee(addressStr))))
        .WillOnce(Return(&mockAddress));

    receiver->handleDataPacket(&mockPacket, &mockDriver);

    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(id, receiver->unregisteredMessages.find(id)->second->getId());
    EXPECT_EQ(id, receiver->receivedMessages.front()->getId());
}

TEST_F(ReceiverTest, receiveMessage)
{
    Receiver::InboundMessage* msg0 = receiver->messagePool.construct();
    Receiver::InboundMessage* msg1 = receiver->messagePool.construct();

    receiver->receivedMessages.push_back(msg0);
    receiver->receivedMessages.push_back(msg1);
    EXPECT_EQ(2U, receiver->receivedMessages.size());

    EXPECT_EQ(msg0, receiver->receiveMessage());
    EXPECT_EQ(1U, receiver->receivedMessages.size());

    EXPECT_EQ(msg1, receiver->receiveMessage());
    EXPECT_EQ(0U, receiver->receivedMessages.size());

    EXPECT_EQ(nullptr, receiver->receiveMessage());
    EXPECT_EQ(0U, receiver->receivedMessages.size());
}

TEST_F(ReceiverTest, dropMessage)
{
    Protocol::MessageId id = {42, 32, 0};
    Receiver::InboundMessage* message = receiver->messagePool.construct();
    message->id = id;
    receiver->unregisteredMessages.insert({id, message});
    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(message, receiver->unregisteredMessages.find(id)->second);

    receiver->dropMessage(message);

    EXPECT_EQ(0U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(receiver->unregisteredMessages.end(),
              receiver->unregisteredMessages.find(id));
}

TEST_F(ReceiverTest, registerOp_existingMessage)
{
    Protocol::MessageId id = {42, 32, 1};
    OpContext op_(nullptr, &mockDriver);
    OpContext* op = &op_;
    Receiver::InboundMessage* message = receiver->messagePool.construct();
    message->id = id;
    receiver->unregisteredMessages.insert({id, message});

    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(receiver->registeredOps.end(), receiver->registeredOps.find(id));
    EXPECT_EQ(message, receiver->unregisteredMessages.find(id)->second);

    receiver->registerOp(id, op);

    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(op, receiver->registeredOps.find(id)->second);
    EXPECT_EQ(
        id,
        receiver->registeredOps.find(id)->second->inMessage.load()->getId());
    EXPECT_EQ(receiver->unregisteredMessages.end(),
              receiver->unregisteredMessages.find(id));
}

TEST_F(ReceiverTest, registerOp_newMessage)
{
    Protocol::MessageId id = {42, 32, 0};
    OpContext op_(nullptr, &mockDriver);
    OpContext* op = &op_;

    EXPECT_EQ(0U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(receiver->registeredOps.end(), receiver->registeredOps.find(id));

    receiver->registerOp(id, op);

    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(op, receiver->registeredOps.find(id)->second);
    EXPECT_EQ(
        id,
        receiver->registeredOps.find(id)->second->inMessage.load()->getId());
}

TEST_F(ReceiverTest, dropOp)
{
    Protocol::MessageId id = {42, 32, 1};
    OpContext op_(nullptr, &mockDriver);
    OpContext* op = &op_;
    Receiver::InboundMessage* message = receiver->messagePool.construct();
    message->id = id;
    op->inMessage = message;
    receiver->registeredOps.insert({id, op});

    EXPECT_EQ(1U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(op, receiver->registeredOps.find(id)->second);
    EXPECT_EQ(
        id,
        receiver->registeredOps.find(id)->second->inMessage.load()->getId());

    receiver->dropOp(op);

    EXPECT_EQ(0U, receiver->messagePool.outstandingObjects);
    EXPECT_EQ(receiver->registeredOps.end(), receiver->registeredOps.find(id));
}

TEST_F(ReceiverTest, poll)
{
    // Nothing to test.
    receiver->poll();
}

}  // namespace
}  // namespace Core
}  // namespace Homa
