#include <gtest/gtest.h>
#include "vssdag/can/can_source.h"
#include "vssdag/can/can_reader.h"
#include "vssdag/can/fake_can_reader.h"
#include "vssdag/mapping_types.h"
#include <fstream>

using namespace vssdag;

class FakeSourceTest : public ::testing::Test {
protected:
    std::string test_dbc_file;

    void SetUp() override {
        test_dbc_file = "test_fake_source.dbc";
        CreateTestDBCFile();
    }

    void TearDown() override {
        std::remove(test_dbc_file.c_str());
    }

    void CreateTestDBCFile() {
        std::ofstream file(test_dbc_file);
        file << "VERSION \"\"\n\n";
        file << "BS_:\n\n";
        file << "BU_: ECU1\n\n";
        file << "BO_ 256 TestMessage: 8 ECU1\n";
        file << " SG_ Speed : 0|16@1+ (0.1,0) [0|6553.5] \"km/h\" ECU1\n";
        file << " SG_ Temperature : 16|8@1+ (1,0) [0|255] \"degC\" ECU1\n";
        file.close();
    }

    std::unordered_map<std::string, SignalMapping> CreateTestMappings() {
        std::unordered_map<std::string, SignalMapping> mappings;

        SignalMapping speed_mapping;
        speed_mapping.datatype = ValueType::FLOAT;
        speed_mapping.transform = DirectMapping{};
        speed_mapping.source.type = "dbc";
        speed_mapping.source.name = "TestMessage.Speed";
        mappings["Vehicle.Speed"] = speed_mapping;

        SignalMapping temp_mapping;
        temp_mapping.datatype = ValueType::FLOAT;
        temp_mapping.transform = DirectMapping{};
        temp_mapping.source.type = "dbc";
        temp_mapping.source.name = "TestMessage.Temperature";
        mappings["Vehicle.Temperature"] = temp_mapping;

        return mappings;
    }

    std::pair<std::unique_ptr<CANSignalSource>, FakeCANReader*> MakeSource() {
        auto mappings = CreateTestMappings();
        auto reader = std::make_unique<FakeCANReader>();
        auto* reader_ptr = reader.get();
        auto source = std::make_unique<CANSignalSource>(std::move(reader), test_dbc_file, mappings);
        return {std::move(source), reader_ptr};
    }
};

TEST_F(FakeSourceTest, InjectSingleFrame) {
    auto [source, reader] = MakeSource();
    ASSERT_TRUE(source->initialize());

    reader->inject_frame(CANFrame{256, {0x64, 0x00, 0x19}, 0});

    auto updates = source->poll();
    EXPECT_EQ(updates.size(), 2);
}

TEST_F(FakeSourceTest, InjectMultipleFrames) {
    auto [source, reader] = MakeSource();
    ASSERT_TRUE(source->initialize());

    reader->inject_frame(CANFrame{256, {0x64, 0x00, 0x19}, 0});
    reader->inject_frame(CANFrame{256, {0xC8, 0x00, 0x1E}, 0});

    auto updates = source->poll();
    EXPECT_EQ(updates.size(), 4);
}

TEST_F(FakeSourceTest, IgnoreUnmappedFrames) {
    auto [source, reader] = MakeSource();
    ASSERT_TRUE(source->initialize());

    reader->inject_frame(CANFrame{999, {0x01, 0x02}, 0});

    auto updates = source->poll();
    EXPECT_EQ(updates.size(), 0);
}

TEST_F(FakeSourceTest, VerifySignalValues) {
    auto [source, reader] = MakeSource();
    ASSERT_TRUE(source->initialize());

    reader->inject_frame(CANFrame{256, {0x64, 0x00, 0x19}, 0});

    auto updates = source->poll();
    ASSERT_EQ(updates.size(), 2);

    for (const auto& update : updates) {
        if (update.signal_name == "Vehicle.Speed") {
            auto speed_val = vss::types::to_double(update.value);
            EXPECT_FLOAT_EQ(speed_val, 10.0);
        } else if (update.signal_name == "Vehicle.Temperature") {
            auto temp_val = vss::types::to_double(update.value);
            EXPECT_FLOAT_EQ(temp_val, 25.0);
        }
    }
}
