// Copyright (c) 2004-present Facebook All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Code generated (@generated) by entc, DO NOT EDIT.

package ent

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/facebookincubator/ent/dialect/sql"
	"github.com/facebookincubator/symphony/graph/ent/equipmentpositiondefinition"
)

// EquipmentPositionDefinition is the model entity for the EquipmentPositionDefinition schema.
type EquipmentPositionDefinition struct {
	config `gqlgen:"-" json:"-"`
	// ID of the ent.
	ID string `json:"id,omitempty"`
	// CreateTime holds the value of the "create_time" field.
	CreateTime time.Time `json:"create_time,omitempty"`
	// UpdateTime holds the value of the "update_time" field.
	UpdateTime time.Time `json:"update_time,omitempty"`
	// Name holds the value of the "name" field.
	Name string `json:"name,omitempty"`
	// Index holds the value of the "index" field.
	Index int `json:"index,omitempty"`
	// VisibilityLabel holds the value of the "visibility_label" field.
	VisibilityLabel string `json:"visibility_label,omitempty" gqlgen:"visibleLabel"`
}

// scanValues returns the types for scanning values from sql.Rows.
func (*EquipmentPositionDefinition) scanValues() []interface{} {
	return []interface{}{
		&sql.NullInt64{},
		&sql.NullTime{},
		&sql.NullTime{},
		&sql.NullString{},
		&sql.NullInt64{},
		&sql.NullString{},
	}
}

// assignValues assigns the values that were returned from sql.Rows (after scanning)
// to the EquipmentPositionDefinition fields.
func (epd *EquipmentPositionDefinition) assignValues(values ...interface{}) error {
	if m, n := len(values), len(equipmentpositiondefinition.Columns); m != n {
		return fmt.Errorf("mismatch number of scan values: %d != %d", m, n)
	}
	value, ok := values[0].(*sql.NullInt64)
	if !ok {
		return fmt.Errorf("unexpected type %T for field id", value)
	}
	epd.ID = strconv.FormatInt(value.Int64, 10)
	values = values[1:]
	if value, ok := values[0].(*sql.NullTime); !ok {
		return fmt.Errorf("unexpected type %T for field create_time", values[0])
	} else if value.Valid {
		epd.CreateTime = value.Time
	}
	if value, ok := values[1].(*sql.NullTime); !ok {
		return fmt.Errorf("unexpected type %T for field update_time", values[1])
	} else if value.Valid {
		epd.UpdateTime = value.Time
	}
	if value, ok := values[2].(*sql.NullString); !ok {
		return fmt.Errorf("unexpected type %T for field name", values[2])
	} else if value.Valid {
		epd.Name = value.String
	}
	if value, ok := values[3].(*sql.NullInt64); !ok {
		return fmt.Errorf("unexpected type %T for field index", values[3])
	} else if value.Valid {
		epd.Index = int(value.Int64)
	}
	if value, ok := values[4].(*sql.NullString); !ok {
		return fmt.Errorf("unexpected type %T for field visibility_label", values[4])
	} else if value.Valid {
		epd.VisibilityLabel = value.String
	}
	return nil
}

// QueryPositions queries the positions edge of the EquipmentPositionDefinition.
func (epd *EquipmentPositionDefinition) QueryPositions() *EquipmentPositionQuery {
	return (&EquipmentPositionDefinitionClient{epd.config}).QueryPositions(epd)
}

// QueryEquipmentType queries the equipment_type edge of the EquipmentPositionDefinition.
func (epd *EquipmentPositionDefinition) QueryEquipmentType() *EquipmentTypeQuery {
	return (&EquipmentPositionDefinitionClient{epd.config}).QueryEquipmentType(epd)
}

// Update returns a builder for updating this EquipmentPositionDefinition.
// Note that, you need to call EquipmentPositionDefinition.Unwrap() before calling this method, if this EquipmentPositionDefinition
// was returned from a transaction, and the transaction was committed or rolled back.
func (epd *EquipmentPositionDefinition) Update() *EquipmentPositionDefinitionUpdateOne {
	return (&EquipmentPositionDefinitionClient{epd.config}).UpdateOne(epd)
}

// Unwrap unwraps the entity that was returned from a transaction after it was closed,
// so that all next queries will be executed through the driver which created the transaction.
func (epd *EquipmentPositionDefinition) Unwrap() *EquipmentPositionDefinition {
	tx, ok := epd.config.driver.(*txDriver)
	if !ok {
		panic("ent: EquipmentPositionDefinition is not a transactional entity")
	}
	epd.config.driver = tx.drv
	return epd
}

// String implements the fmt.Stringer.
func (epd *EquipmentPositionDefinition) String() string {
	var builder strings.Builder
	builder.WriteString("EquipmentPositionDefinition(")
	builder.WriteString(fmt.Sprintf("id=%v", epd.ID))
	builder.WriteString(", create_time=")
	builder.WriteString(epd.CreateTime.Format(time.ANSIC))
	builder.WriteString(", update_time=")
	builder.WriteString(epd.UpdateTime.Format(time.ANSIC))
	builder.WriteString(", name=")
	builder.WriteString(epd.Name)
	builder.WriteString(", index=")
	builder.WriteString(fmt.Sprintf("%v", epd.Index))
	builder.WriteString(", visibility_label=")
	builder.WriteString(epd.VisibilityLabel)
	builder.WriteByte(')')
	return builder.String()
}

// id returns the int representation of the ID field.
func (epd *EquipmentPositionDefinition) id() int {
	id, _ := strconv.Atoi(epd.ID)
	return id
}

// EquipmentPositionDefinitions is a parsable slice of EquipmentPositionDefinition.
type EquipmentPositionDefinitions []*EquipmentPositionDefinition

func (epd EquipmentPositionDefinitions) config(cfg config) {
	for _i := range epd {
		epd[_i].config = cfg
	}
}
