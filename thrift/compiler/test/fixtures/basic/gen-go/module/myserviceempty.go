// Autogenerated by Thrift Compiler (facebook)
// DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
// @generated

package module

import (
	"bytes"
	"context"
	"sync"
	"fmt"
	thrift "github.com/facebook/fbthrift-go"
)

// (needed to ensure safety because of naive import list construction.)
var _ = thrift.ZERO
var _ = fmt.Printf
var _ = sync.Mutex{}
var _ = bytes.Equal
var _ = context.Background

type MyServiceEmpty interface {}

type MyServiceEmptyClient struct {
  CC thrift.ClientConn
}

func(client *MyServiceEmptyClient) Open() error {
  return client.CC.Open()
}

func(client *MyServiceEmptyClient) Close() error {
  return client.CC.Close()
}

func(client *MyServiceEmptyClient) IsOpen() bool {
  return client.CC.IsOpen()
}

func NewMyServiceEmptyClientFactory(t thrift.Transport, f thrift.ProtocolFactory) *MyServiceEmptyClient {
  return &MyServiceEmptyClient{ CC: thrift.NewClientConn(t, f) }
}

func NewMyServiceEmptyClient(t thrift.Transport, iprot thrift.Protocol, oprot thrift.Protocol) *MyServiceEmptyClient {
  return &MyServiceEmptyClient{ CC: thrift.NewClientConnWithProtocols(t, iprot, oprot) }
}


type MyServiceEmptyThreadsafeClient struct {
  CC thrift.ClientConn
  Mu sync.Mutex
}

func(client *MyServiceEmptyThreadsafeClient) Open() error {
  client.Mu.Lock()
  defer client.Mu.Unlock()
  return client.CC.Open()
}

func(client *MyServiceEmptyThreadsafeClient) Close() error {
  client.Mu.Lock()
  defer client.Mu.Unlock()
  return client.CC.Close()
}

func(client *MyServiceEmptyThreadsafeClient) IsOpen() bool {
  client.Mu.Lock()
  defer client.Mu.Unlock()
  return client.CC.IsOpen()
}

func NewMyServiceEmptyThreadsafeClientFactory(t thrift.Transport, f thrift.ProtocolFactory) *MyServiceEmptyThreadsafeClient {
  return &MyServiceEmptyThreadsafeClient{ CC: thrift.NewClientConn(t, f) }
}

func NewMyServiceEmptyThreadsafeClient(t thrift.Transport, iprot thrift.Protocol, oprot thrift.Protocol) *MyServiceEmptyThreadsafeClient {
  return &MyServiceEmptyThreadsafeClient{ CC: thrift.NewClientConnWithProtocols(t, iprot, oprot) }
}


type MyServiceEmptyProcessor struct {
  processorMap map[string]thrift.ProcessorFunction
  handler MyServiceEmpty
}

func (p *MyServiceEmptyProcessor) AddToProcessorMap(key string, processor thrift.ProcessorFunction) {
  p.processorMap[key] = processor
}

func (p *MyServiceEmptyProcessor) GetProcessorFunction(key string) (processor thrift.ProcessorFunction, err error) {
  if processor, ok := p.processorMap[key]; ok {
    return processor, nil
  }
  return nil, nil // generic error message will be sent
}

func (p *MyServiceEmptyProcessor) ProcessorMap() map[string]thrift.ProcessorFunction {
  return p.processorMap
}

func NewMyServiceEmptyProcessor(handler MyServiceEmpty) *MyServiceEmptyProcessor {
  self16 := &MyServiceEmptyProcessor{handler:handler, processorMap:make(map[string]thrift.ProcessorFunction)}
  return self16
}


// HELPER FUNCTIONS AND STRUCTURES


