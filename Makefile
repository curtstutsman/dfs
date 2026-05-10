CXX      = g++
CXXFLAGS = -Wall -g3 -fPIC -std=c++23 -I.
CPPFLAGS = $(shell pkg-config --cflags protobuf grpc)
LDFLAGS  = $(shell pkg-config --libs protobuf grpc++ grpc) \
           -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed -ldl
ASAN      ?= -fsanitize=address -fno-omit-frame-pointer -static-libasan
GTEST_LIBS = -lgtest -lgtest_main -lpthread

PROTOC      = protoc
GRPC_PLUGIN = $(shell which grpc_cpp_plugin)

BIN = bin
OBJ = tmp

PROTO_OBJS  = $(OBJ)/dfs-service.pb.o $(OBJ)/dfs-service.grpc.pb.o
COMMON_OBJS =
CLIENT_OBJS = $(OBJ)/ClientNode.o $(OBJ)/Client.o
SERVER_OBJS = $(OBJ)/ServiceImpl.o $(OBJ)/FileStore.o
TEST_OBJS   = $(OBJ)/ServiceTest.o

vpath %.cpp src/common src/client src/server tests
vpath %.cc  proto-src

all: $(BIN)/dfs-client $(BIN)/dfs-server

$(BIN)/dfs-client: $(PROTO_OBJS) $(COMMON_OBJS) $(CLIENT_OBJS) client.cpp
	$(CXX) $^ $(CXXFLAGS) $(CPPFLAGS) $(ASAN) $(LDFLAGS) -o $@

$(BIN)/dfs-server: $(PROTO_OBJS) $(COMMON_OBJS) $(SERVER_OBJS) server.cpp
	$(CXX) $^ $(CXXFLAGS) $(CPPFLAGS) $(ASAN) $(LDFLAGS) -o $@

# Tests link ServiceImpl + ClientNode directly; no ASAN to avoid false leak reports from
# intentionally long-lived server threads.
$(BIN)/dfs-tests: $(PROTO_OBJS) $(COMMON_OBJS) $(OBJ)/ServiceImpl.o $(OBJ)/FileStore.o $(OBJ)/ClientNode.o $(TEST_OBJS)
	$(CXX) $^ $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(GTEST_LIBS) -o $@

test: $(BIN)/dfs-tests
	./$(BIN)/dfs-tests

$(OBJ)/%.o: %.cpp
	$(CXX) $< -c $(CXXFLAGS) $(CPPFLAGS) -o $@

$(OBJ)/%.o: %.cc
	$(CXX) $< -c $(CPPFLAGS) -o $@

protos:
	$(PROTOC) -I proto --cpp_out=proto-src --grpc_out=proto-src \
	    --plugin=protoc-gen-grpc=$(GRPC_PLUGIN) proto/dfs-service.proto

dirs:
	mkdir -p $(BIN) $(OBJ) mnt/client mnt/server

clean:
	rm -f $(BIN)/dfs-client $(BIN)/dfs-server $(BIN)/dfs-tests $(OBJ)/*.o

clean_all: clean
	rm -f proto-src/*.pb.cc proto-src/*.pb.h

.PHONY: all protos dirs clean clean_all test
