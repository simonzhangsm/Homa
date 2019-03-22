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

#include "Receiver.h"

#include "OpContext.h"

namespace Homa {
namespace Core {

/**
 * Receiver constructor.
 *
 * @param scheduler
 *      Scheduler that should be informed when message packets are received.
 */
Receiver::Receiver(Scheduler* scheduler)
    : mutex()
    , scheduler(scheduler)
    , registeredOps()
    , unregisteredMessages()
    , receivedMessages()
    , messagePool()
{}

/**
 * Receiver distructor.
 */
Receiver::~Receiver() {}

/**
 * Process an incoming DATA packet.
 *
 * @param packet
 *      The incoming packet to be processed.
 * @param driver
 *      The driver from which the packet was received.
 * @return
 *      OpContext which holds the Message associated with the incomming packet,
 *      if the Message is fully received; otherwise, nullptr.
 */
OpContext*
Receiver::handleDataPacket(Driver::Packet* packet, Driver* driver)
{
    Protocol::Packet::DataHeader* header =
        static_cast<Protocol::Packet::DataHeader*>(packet->payload);
    uint16_t dataHeaderLength = sizeof(Protocol::Packet::DataHeader);
    Protocol::MessageId id = header->common.messageId;

    OpContext* op = nullptr;
    InboundMessage* message = nullptr;
    {
        std::lock_guard<SpinLock> lock(mutex);
        auto it = registeredOps.find(id);
        if (it != registeredOps.end()) {
            // Registered Op
            op = it->second;
            assert(op->inMessage != nullptr);
            message = op->inMessage;
        } else {
            // Unregistered Message
            auto it = unregisteredMessages.find(id);
            if (it != unregisteredMessages.end()) {
                // Existing unregistered message
                message = it->second;
            } else {
                // New unregistered message
                message = messagePool.construct();
                // Touch OK w/o lock before externalizing.
                message->id = id;
                unregisteredMessages.insert(it, {id, message});
                receivedMessages.push_back(message);
            }
        }
        message->mutex.lock();
    }

    std::lock_guard<SpinLock> lock_message(message->mutex, std::adopt_lock);
    assert(id == message->id);
    if (!message->message) {
        uint32_t messageLength = header->totalLength;
        message->message.construct(driver, dataHeaderLength, messageLength);
        // Get an address pointer from the driver; the one in the packet
        // may disappear when the packet goes away.
        std::string addrStr = packet->address->toString();
        message->source = driver->getAddress(&addrStr);
    }

    // All packets already received; must be a duplicate.
    if (message->fullMessageReceived) {
        // drop packet
        driver->releasePackets(&packet, 1);
        return nullptr;
    }

    // Things that must be true (sanity check)
    assert(message->source->toString() == packet->address->toString());
    assert(message->message->rawLength() == header->totalLength);

    // Add the packet
    bool packetAdded = message->message->setPacket(header->index, packet);
    if (packetAdded) {
        // This value is technically sloppy since last packet of the message
        // which may not be a full packet. However, this should be fine since
        // receiving the last packet means we don't need the scheduler to GRANT
        // more packets anyway.
        uint32_t totalReceivedBytes = message->message->PACKET_DATA_LENGTH *
                                      message->message->getNumPackets();

        // Let the Scheduler know that we received a packet.
        scheduler->packetReceived(id, message->source,
                                  message->message->rawLength(),
                                  totalReceivedBytes);
        if (totalReceivedBytes >= message->message->rawLength()) {
            message->fullMessageReceived = true;
        } else {
            // Message not fully received
            op = nullptr;
        }
    } else {
        // must be a duplicate packet; drop packet.
        driver->releasePackets(&packet, 1);
        op = nullptr;
    }
    return op;
}

/**
 * Return an InboundMessage that has not been registered with an OpContext.
 *
 * The Transport should regularly call this method to insure incomming messages
 * are processed.  The Transport can choose to register the InboundMessage with
 * an OpContext or drop the message if it is not of interest.
 *
 * Returned message may not be fully received.  The Receiver will continue to
 * process packets into the returned InboundMessage until it is dropped.
 *
 * @return
 *      A new InboundMessage which has been at least partially received but not
 *      register, if available; otherwise, nullptr.
 *
 * @sa registerOp(), dropMessage()
 */
Receiver::InboundMessage*
Receiver::receiveMessage()
{
    std::lock_guard<SpinLock> lock(mutex);
    InboundMessage* message = nullptr;
    if (!receivedMessages.empty()) {
        message = receivedMessages.front();
        receivedMessages.pop_front();
    }
    return message;
}

/**
 * Inform the Receiver that an InboundMessage returned by receiveMessage() is
 * not needed and can be dropped.
 *
 * @param message
 *      InboundMessage which will be dropped.
 */
void
Receiver::dropMessage(InboundMessage* message)
{
    std::lock_guard<SpinLock> lock(mutex);
    message->mutex.lock();
    unregisteredMessages.erase(message->id);
    messagePool.destroy(message);
}

/**
 * Inform the Receiver that an incomming Message is expected and should be
 * associated with a particular OpContext.
 *
 * @param id
 *      Id of the Message that should be expected.
 * @param op
 *      OpContext where the expected Message should be accumulated.
 */
void
Receiver::registerOp(Protocol::MessageId id, OpContext* op)
{
    std::lock_guard<SpinLock> lock(mutex);
    InboundMessage* message;
    auto it = unregisteredMessages.find(id);
    if (it != unregisteredMessages.end()) {
        // Existing message
        message = it->second;
        unregisteredMessages.erase(it);
    } else {
        // New message
        message = messagePool.construct();
        // Touch OK w/o lock before externalizing.
        message->id = id;
    }
    op->inMessage = message;
    registeredOps.insert({id, op});
}

/**
 * Inform the Receiver that a Message is no longer needed and the associated
 * OpContext should no longer be used.
 *
 * @param op
 *      The OpContext which contains the Message that is no longer needed.
 */
void
Receiver::dropOp(OpContext* op)
{
    std::lock_guard<SpinLock> lock(mutex);
    assert(op->inMessage != nullptr);
    InboundMessage* message = op->inMessage;
    message->mutex.lock();
    op->inMessage = nullptr;
    registeredOps.erase(message->id);
    messagePool.destroy(message);
}

/**
 * Allow the Receiver to make incremental progress on background tasks.
 */
void
Receiver::poll()
{}

}  // namespace Core
}  // namespace Homa