#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "proto-src/dfs-service.grpc.pb.h"
#include "src/client/ClientNode.hpp"
#include "src/server/ServiceImpl.hpp"

namespace {

const std::string kAddr      = "localhost:53999";
const std::string kServerMnt = "tmp/test-server/";
const std::string kClientMnt = "tmp/test-client/";

void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

void clear_dir(const std::string& path) {
    for (const auto& e : std::filesystem::directory_iterator(path))
        std::filesystem::remove_all(e);
}

} // namespace

// Server and client are intentionally leaked — grpc async threads run until process exit.
class ServiceTest : public ::testing::Test {
protected:
    static DFSServiceImpl* svc;
    static ClientNode*  client;

    static void SetUpTestSuite() {
        std::filesystem::create_directories(kServerMnt);
        std::filesystem::create_directories(kClientMnt);

        svc = new DFSServiceImpl(kServerMnt, kAddr, 2);
        std::thread([] { svc->Run(); }).detach();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        client = new ClientNode();
        client->CreateStub(grpc::CreateChannel(kAddr, grpc::InsecureChannelCredentials()));
        client->SetMountPath(kClientMnt);
        client->SetDeadlineTimeout(5000);
        client->SetClientId("test-client-a");
    }

    void SetUp() override {
        clear_dir(kServerMnt);
        clear_dir(kClientMnt);
    }
};

DFSServiceImpl* ServiceTest::svc    = nullptr;
ClientNode*  ServiceTest::client = nullptr;

// ── Store ────────────────────────────────────────────────────────────────────

TEST_F(ServiceTest, Store_NewFile_Succeeds) {
    write_file(kClientMnt + "hello.txt", "hello world");
    EXPECT_EQ(client->Store("hello.txt"), grpc::StatusCode::OK);
    EXPECT_EQ(read_file(kServerMnt + "hello.txt"), "hello world");
}

TEST_F(ServiceTest, Store_UnchangedFile_ReturnsAlreadyExists) {
    write_file(kClientMnt + "same.txt", "no change");
    ASSERT_EQ(client->Store("same.txt"), grpc::StatusCode::OK);
    EXPECT_EQ(client->Store("same.txt"), grpc::StatusCode::ALREADY_EXISTS);
}

TEST_F(ServiceTest, Store_LargeFile_Succeeds) {
    // 64 KB spans 16 CHUNK_SIZE chunks
    const std::string data(64u * 1024u, 'x');
    write_file(kClientMnt + "large.bin", data);
    EXPECT_EQ(client->Store("large.bin"), grpc::StatusCode::OK);
    EXPECT_EQ(read_file(kServerMnt + "large.bin"), data);
}

// ── Fetch ────────────────────────────────────────────────────────────────────

TEST_F(ServiceTest, Fetch_ExistingFile_Succeeds) {
    write_file(kServerMnt + "fetched.txt", "server content");
    EXPECT_EQ(client->Fetch("fetched.txt"), grpc::StatusCode::OK);
    EXPECT_EQ(read_file(kClientMnt + "fetched.txt"), "server content");
}

TEST_F(ServiceTest, Fetch_NonexistentFile_ReturnsNotFound) {
    EXPECT_EQ(client->Fetch("missing.txt"), grpc::StatusCode::NOT_FOUND);
}

TEST_F(ServiceTest, Fetch_UnchangedFile_ReturnsAlreadyExists) {
    write_file(kServerMnt + "same.txt", "content");
    ASSERT_EQ(client->Fetch("same.txt"), grpc::StatusCode::OK);
    EXPECT_EQ(client->Fetch("same.txt"), grpc::StatusCode::ALREADY_EXISTS);
}

// ── Delete ───────────────────────────────────────────────────────────────────

TEST_F(ServiceTest, Delete_ExistingFile_Succeeds) {
    write_file(kClientMnt + "todelete.txt", "bye");
    ASSERT_EQ(client->Store("todelete.txt"), grpc::StatusCode::OK);
    EXPECT_EQ(client->Delete("todelete.txt"), grpc::StatusCode::OK);
    EXPECT_FALSE(std::filesystem::exists(kServerMnt + "todelete.txt"));
}

TEST_F(ServiceTest, Delete_NonexistentFile_ReturnsNotFound) {
    EXPECT_EQ(client->Delete("ghost.txt"), grpc::StatusCode::NOT_FOUND);
}

// ── List ─────────────────────────────────────────────────────────────────────

TEST_F(ServiceTest, List_EmptyServer_ReturnsOk) {
    std::map<std::string, int> files;
    EXPECT_EQ(client->List(&files), grpc::StatusCode::OK);
    EXPECT_TRUE(files.empty());
}

TEST_F(ServiceTest, List_WithFiles_ReturnsAllFilenames) {
    write_file(kClientMnt + "a.txt", "a");
    write_file(kClientMnt + "b.txt", "b");
    ASSERT_EQ(client->Store("a.txt"), grpc::StatusCode::OK);
    ASSERT_EQ(client->Store("b.txt"), grpc::StatusCode::OK);

    std::map<std::string, int> files;
    EXPECT_EQ(client->List(&files), grpc::StatusCode::OK);
    EXPECT_EQ(files.size(), 2u);
    EXPECT_TRUE(files.count("a.txt"));
    EXPECT_TRUE(files.count("b.txt"));
}

// ── Stat ─────────────────────────────────────────────────────────────────────

TEST_F(ServiceTest, Stat_ExistingFile_ReturnsSizeAndMtime) {
    const std::string content = "stat me";
    write_file(kClientMnt + "stat.txt", content);
    ASSERT_EQ(client->Store("stat.txt"), grpc::StatusCode::OK);

    dfs_service::StatResponse stat;
    EXPECT_EQ(client->Stat("stat.txt", &stat), grpc::StatusCode::OK);
    EXPECT_EQ(stat.size(), static_cast<int64_t>(content.size()));
    EXPECT_GT(stat.mtime(), 0);
}

TEST_F(ServiceTest, Stat_NonexistentFile_ReturnsNotFound) {
    dfs_service::StatResponse stat;
    EXPECT_EQ(client->Stat("nothing.txt", &stat), grpc::StatusCode::NOT_FOUND);
}

// ── WriteLock ────────────────────────────────────────────────────────────────

TEST_F(ServiceTest, WriteLock_Contention_SecondClientBlocked) {
    // client_b: stack-allocated, sync-only (no async ops on its completion queue → safe to destroy)
    ClientNode client_b;
    client_b.CreateStub(grpc::CreateChannel(kAddr, grpc::InsecureChannelCredentials()));
    client_b.SetMountPath(kClientMnt);
    client_b.SetDeadlineTimeout(5000);
    client_b.SetClientId("test-client-b");

    write_file(kClientMnt + "contested.txt", "v1");

    // Client A acquires write lock; client B must be rejected
    EXPECT_EQ(client->RequestWriteAccess("contested.txt"), grpc::StatusCode::OK);
    EXPECT_EQ(client_b.RequestWriteAccess("contested.txt"), grpc::StatusCode::RESOURCE_EXHAUSTED);

    // Client A completes the store, releasing the lock
    EXPECT_EQ(client->Store("contested.txt"), grpc::StatusCode::OK);
}
