/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

package servicers

import (
	"fmt"
	"strings"

	"magma/feg/gateway/multiplex"

	"golang.org/x/net/context"

	"magma/feg/cloud/go/protos"
	fegprotos "magma/feg/cloud/go/protos"
	orcprotos "magma/orc8r/lib/go/protos"
)

/*
* ##########################################
* How SwxProxies works
*
* SwxProxies holds an slice of N Proxies. Each proxy uses an particular diameter
* client to an specific HSS. Each diameter client is configured with its own src and dst port
*
* Subscribers are forwarded to a different proxies based on their IMSI that comes either in Authenticate, Register,
* Deregister requests
*
* Health, Enable and Disable returns error if any of the proxies return errors. No partial results are give.
* ##########################################
 */

// SwxProxiesWithHealth is an interface just to group SwxProxies and ServiceHealthServer.
// This is used to be able to return either SwxProxies or SwxProxy (without S)
type SwxProxiesWithHealth interface {
	protos.SwxProxyServer
	fegprotos.ServiceHealthServer
}

type SwxProxies struct {
	proxies     []*swxProxy
	multiplexor multiplex.Multiplexor
}

// NewSwxProxies creates several SwxProxys but it uses a shared cache fir all of them
func NewSwxProxies(configs []*SwxProxyConfig, mux multiplex.Multiplexor) (*SwxProxies, error) {
	// if no servers configured, just exit and avoid crashing
	if len(configs) == 0 {
		return &SwxProxies{}, nil
	}
	// create a shared cache for all the proxies
	cache := createCache(configs[0])
	proxies := make([]*swxProxy, 0, len(configs))
	for i, config := range configs {
		fixConfigCacheMinTTL(config)
		proxy, err := NewSwxProxyWithCache(config, cache)
		if err != nil {
			return nil, err
		}
		proxies[i] = proxy
	}
	swxProxies := &SwxProxies{
		proxies:     proxies,
		multiplexor: mux,
	}
	return swxProxies, nil
}

// NewSwxProxiesWithHealthAndDefaultMultiplex creates either a single swxProxy or a SwxProxies
// In case of SwxProxies it uses StaticMultiplexByIMSI as a multiplexer
func NewSwxProxiesWithHealthAndDefaultMultiplexor(
	configs []*SwxProxyConfig,
) (SwxProxiesWithHealth, error) {
	// if there is only one just return regular SwxProxy
	if len(configs) == 1 {
		return NewSwxProxy(configs[0])
	}
	// uses a StaticMultiplexByIMSI with a length of the configuration
	mux, err := multiplex.NewStaticMultiplexByIMSI(len(configs))
	if err != nil {
		return nil, err
	}
	return NewSwxProxies(configs, mux)
}

// Calls Authenticate on the chosen swx proxy based on IMSI of the incoming request
func (s *SwxProxies) Authenticate(ctx context.Context, req *protos.AuthenticationRequest) (*protos.AuthenticationAnswer, error) {
	imsi := req.GetUserName()
	proxy, err := getProxyPerKey(imsi, s.proxies, s.multiplexor)
	if err != nil {
		return nil, err
	}
	return proxy.Authenticate(ctx, req)
}

// Calls Register on the chosen swx proxy based on IMSI of the incoming request
func (s *SwxProxies) Register(ctx context.Context, req *protos.RegistrationRequest) (*protos.RegistrationAnswer, error) {
	imsi := req.GetUserName()
	proxy, err := getProxyPerKey(imsi, s.proxies, s.multiplexor)
	if err != nil {
		return nil, err
	}
	return proxy.Register(ctx, req)
}

// Calls Deregister on the chosen swx proxy based on IMSI of the incoming request
func (s *SwxProxies) Deregister(ctx context.Context, req *protos.RegistrationRequest) (*protos.RegistrationAnswer, error) {
	imsi := req.GetUserName()
	proxy, err := getProxyPerKey(imsi, s.proxies, s.multiplexor)
	if err != nil {
		return nil, err
	}
	return proxy.Deregister(ctx, req)
}

// Calls Disable on each swx proxy
func (s *SwxProxies) Disable(ctx context.Context, req *protos.DisableMessage) (*orcprotos.Void, error) {
	if req == nil {
		return nil, fmt.Errorf("Nil Disable Request")
	}
	for _, proxy := range s.proxies {
		proxy.Disable(ctx, req)
	}
	return &orcprotos.Void{}, nil
}

// Calls Enable on each swx proxy
func (s *SwxProxies) Enable(ctx context.Context, req *orcprotos.Void) (*orcprotos.Void, error) {
	var hasErrors []string
	for _, proxy := range s.proxies {
		proxy.connMan.Enable()
		_, err := proxy.connMan.GetConnection(proxy.smClient, proxy.config.ServerCfg)
		if err != nil {
			hasErrors = append(hasErrors, err.Error())
		}
	}
	if hasErrors != nil {
		return nil, fmt.Errorf("Errors found while disabling SwxProxies: %s",
			fmt.Errorf(strings.Join(hasErrors, "\n")))
	}
	return &orcprotos.Void{}, nil
}

// Calls GetHealthStatus on each Swx Proxy
func (s *SwxProxies) GetHealthStatus(ctx context.Context, req *orcprotos.Void) (*protos.HealthStatus, error) {
	for _, proxy := range s.proxies {
		healthMessage, err := proxy.GetHealthStatus(ctx, req)
		if err != nil || healthMessage.Health == protos.HealthStatus_UNHEALTHY {
			return healthMessage, err
		}
	}
	return &protos.HealthStatus{
		Health:        protos.HealthStatus_HEALTHY,
		HealthMessage: "All metrics appear healthy",
	}, nil
}

// getProxyPerKey provides the proxy per a given IMSI
func getProxyPerKey(imsi string, proxies []*swxProxy, mux multiplex.Multiplexor) (*swxProxy, error) {
	index, err := mux.GetIndex(multiplex.NewContext().WithIMSI(imsi))
	if err != nil {
		return nil, err
	}
	if index >= len(proxies) {
		return nil, fmt.Errorf("Index %d is bigger than the ammount of proxies %d", index, len(proxies))
	}
	return proxies[index], nil
}