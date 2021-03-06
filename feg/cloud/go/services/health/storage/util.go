/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

package storage

import (
	"time"

	fegprotos "magma/feg/cloud/go/protos"
	"magma/feg/cloud/go/services/health"
	"magma/orc8r/cloud/go/blobstore"
	"magma/orc8r/cloud/go/clock"
	"magma/orc8r/cloud/go/protos"
)

// HealthToBlob converts a gatewayID and healthStats proto to a Blobstore blob
func HealthToBlob(gatewayID string, healthStats *fegprotos.HealthStats) (blobstore.Blob, error) {
	marshaledHealth, err := protos.Marshal(healthStats)
	if err != nil {
		return blobstore.Blob{}, err
	}
	return blobstore.Blob{
		Type:  health.HealthStatusType,
		Key:   gatewayID,
		Value: marshaledHealth,
	}, nil
}

// ClusterToBlob converts a clusterID and activeID to a Blobstore blob
func ClusterToBlob(clusterID string, activeID string) (blobstore.Blob, error) {
	clusterState := &fegprotos.ClusterState{
		ActiveGatewayLogicalId: activeID,
		Time:                   uint64(clock.Now().UnixNano()) / uint64(time.Millisecond),
	}
	marsheledCluster, err := protos.Marshal(clusterState)
	if err != nil {
		return blobstore.Blob{}, err
	}
	return blobstore.Blob{
		Type:  health.ClusterStatusType,
		Key:   clusterID,
		Value: marsheledCluster,
	}, nil
}
