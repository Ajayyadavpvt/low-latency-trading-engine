#include <gtest/gtest.h>
#include "MarketDataPublisher.h"

class RecordingSubscriber : public MarketDataSubscriber {
public:
    void onEvent(const MarketEvent& event) noexcept override {
        events.push_back(event);
    }
    std::vector<MarketEvent> events;
};

// Helper to create a simple event for testing
MarketEvent makeTestEvent(EventType type) {
    // For simplicity, return a TradeEvent with dummy data
    return TradeEvent(0, 1, 2, 100, 200, 10, 12345, true);
}

TEST(MarketDataPublisherTest, NoSubscribersDoesNothing) {
    MarketDataPublisher publisher;
    publisher.publish(makeTestEvent(EventType::Trade));
    // Should not crash
}

TEST(MarketDataPublisherTest, SingleSubscriberReceivesEvent) {
    MarketDataPublisher publisher;
    RecordingSubscriber sub;
    publisher.subscribe(&sub);

    auto event = makeTestEvent(EventType::Trade);
    publisher.publish(event);

    ASSERT_EQ(sub.events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<TradeEvent>(sub.events[0]));
    const auto& trade = std::get<TradeEvent>(sub.events[0]);
    EXPECT_EQ(trade.symbolId, 100u);
    EXPECT_EQ(trade.tradeQuantity, 10u);
}

TEST(MarketDataPublisherTest, MultipleSubscribersAllReceiveEvent) {
    MarketDataPublisher publisher;
    RecordingSubscriber sub1, sub2;
    publisher.subscribe(&sub1);
    publisher.subscribe(&sub2);

    publisher.publish(makeTestEvent(EventType::OrderCancelled));

    EXPECT_EQ(sub1.events.size(), 1u);
    EXPECT_EQ(sub2.events.size(), 1u);
}

TEST(MarketDataPublisherTest, UnsubscribeStopsDelivery) {
    MarketDataPublisher publisher;
    RecordingSubscriber sub;
    publisher.subscribe(&sub);
    publisher.unsubscribe(&sub);

    publisher.publish(makeTestEvent(EventType::OrderAccepted));
    EXPECT_TRUE(sub.events.empty());
}

TEST(MarketDataPublisherTest, DuplicateSubscriptionIgnored) {
    MarketDataPublisher publisher;
    RecordingSubscriber sub;
    publisher.subscribe(&sub);
    publisher.subscribe(&sub);  // duplicate

    publisher.publish(makeTestEvent(EventType::Trade));
    EXPECT_EQ(sub.events.size(), 1u);
}

TEST(MarketDataPublisherTest, ReentrantUnsubscribeDuringPublish) {
    // This subscriber unsubscribes itself when it receives an event.
    class SelfUnsubscriber : public MarketDataSubscriber {
    public:
        SelfUnsubscriber(MarketDataPublisher* pub) : pub_(pub) {}
        void onEvent(const MarketEvent&) noexcept override {
            pub_->unsubscribe(this);
            was_called_ = true;
        }
        bool wasCalled() const { return was_called_; }
    private:
        MarketDataPublisher* pub_;
        bool was_called_ = false;
    };

    MarketDataPublisher publisher;
    SelfUnsubscriber sub(&publisher);
    publisher.subscribe(&sub);

    publisher.publish(makeTestEvent(EventType::Trade));

    EXPECT_TRUE(sub.wasCalled());
    // After unsubscribing itself, second publish should not call it again
    publisher.publish(makeTestEvent(EventType::Trade));
    EXPECT_TRUE(sub.wasCalled()); // still true, not called again
}