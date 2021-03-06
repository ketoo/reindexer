package builtinserver

// #include "server/cbinding/server_c.h"
// #include <stdlib.h>
import "C"
import (
	"fmt"
	"net/url"
	"reflect"
	"time"
	"unsafe"

	"github.com/restream/reindexer/bindings"
	"github.com/restream/reindexer/bindings/builtin"
	"github.com/restream/reindexer/bindings/builtinserver/config"
)

var defaultStartupTimeout time.Duration = time.Minute * 3

func init() {
	C.init_reindexer_server()
	bindings.RegisterBinding("builtinserver", new(BuiltinServer))
}

func err2go(ret C.reindexer_error) error {
	if ret.what != nil {
		defer C.free(unsafe.Pointer(ret.what))
		return bindings.NewError("rq:"+C.GoString(ret.what), int(ret.code))
	}
	return nil
}

func str2c(str string) C.reindexer_string {
	hdr := (*reflect.StringHeader)(unsafe.Pointer(&str))
	return C.reindexer_string{p: unsafe.Pointer(hdr.Data), n: C.int(hdr.Len)}
}

func checkStorageReady() bool {
	return int(C.check_server_ready()) == 1
}

type BuiltinServer struct {
	builtin bindings.RawBinding
	ready   chan bool
}

func (server *BuiltinServer) Init(u *url.URL, options ...interface{}) error {
	server.builtin = &builtin.Builtin{}

	startupTimeout := defaultStartupTimeout
	serverCfg := config.DefaultServerConfig()

	for _, option := range options {
		switch v := option.(type) {
		case bindings.OptionCgoLimit:
		case bindings.OptionBuiltinWithServer:
			if v.StartupTimeout != 0 {
				startupTimeout = v.StartupTimeout
			}
			if v.ServerConfig != nil {
				serverCfg = v.ServerConfig
			}
		default:
			fmt.Printf("Unknown builtinserver option: %v\n", option)
		}
	}

	yamlStr, err := serverCfg.GetYamlString()
	if err != nil {
		return err
	}

	go func() {
		err := err2go(C.start_reindexer_server(str2c(yamlStr)))
		if err != nil {
			panic(err)
		}
	}()

	tTimeout := time.Now().Add(startupTimeout)
	for !checkStorageReady() {
		if time.Now().After(tTimeout) {
			panic(bindings.NewError("Server startup timeout expired.", bindings.ErrLogic))
		}
		time.Sleep(time.Second)
	}

	pass, _ := u.User.Password()
	server.builtin.(*builtin.Builtin).SetReindexerInstance(
		unsafe.Pointer(C.get_reindexer_instance(str2c(u.Host), str2c(u.User.Username()), str2c(pass))),
	)

	url := *u
	url.Path = ""

	if err := server.builtin.Init(&url, options...); err != nil {
		return err
	}

	return nil
}

func (server *BuiltinServer) OpenNamespace(namespace string, enableStorage, dropOnFileFormatError bool, cacheMode uint8) error {
	return server.builtin.OpenNamespace(namespace, enableStorage, dropOnFileFormatError, cacheMode)
}

func (server *BuiltinServer) CloseNamespace(namespace string) error {
	return server.builtin.CloseNamespace(namespace)
}

func (server *BuiltinServer) DropNamespace(namespace string) error {
	return server.builtin.DropNamespace(namespace)
}

func (server *BuiltinServer) EnableStorage(namespace string) error {
	return server.builtin.EnableStorage(namespace)
}

func (server *BuiltinServer) AddIndex(namespace, index, jsonPath, indexType, fieldType string, opts bindings.IndexOptions, collateMode int, sortOrderStr string) error {
	return server.builtin.AddIndex(namespace, index, jsonPath, indexType, fieldType, opts, collateMode, sortOrderStr)
}

func (server *BuiltinServer) DropIndex(namespace, index string) error {
	return server.builtin.DropIndex(namespace, index)
}

func (server *BuiltinServer) ConfigureIndex(namespace, index, config string) error {
	return server.ConfigureIndex(namespace, index, config)
}

func (server *BuiltinServer) PutMeta(namespace, key, data string) error {
	return server.builtin.PutMeta(namespace, key, data)
}

func (server *BuiltinServer) GetMeta(namespace, key string) (bindings.RawBuffer, error) {
	return server.builtin.GetMeta(namespace, key)
}

func (server *BuiltinServer) ModifyItem(nsHash int, data []byte, mode int) (bindings.RawBuffer, error) {
	return server.builtin.ModifyItem(nsHash, data, mode)
}

func (server *BuiltinServer) Select(query string, withItems bool, ptVersions []int32, fetchCount int) (bindings.RawBuffer, error) {
	return server.builtin.Select(query, withItems, ptVersions, fetchCount)
}

func (server *BuiltinServer) SelectQuery(rawQuery []byte, withItems bool, ptVersions []int32, fetchCount int) (bindings.RawBuffer, error) {
	return server.builtin.SelectQuery(rawQuery, withItems, ptVersions, fetchCount)
}

func (server *BuiltinServer) DeleteQuery(nsHash int, rawQuery []byte) (bindings.RawBuffer, error) {
	return server.builtin.DeleteQuery(nsHash, rawQuery)
}

func (server *BuiltinServer) Commit(namespace string) error {
	return server.builtin.Commit(namespace)
}

func (server *BuiltinServer) EnableLogger(logger bindings.Logger) {
	server.builtin.EnableLogger(logger)
}

func (server *BuiltinServer) DisableLogger() {
	server.builtin.DisableLogger()
}

func (server *BuiltinServer) Ping() error {
	return server.builtin.Ping()
}
