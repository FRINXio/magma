/**
 * @generated
 * Copyright 2004-present Facebook. All Rights Reserved.
 *
 **/

 /**
 * @flow
 * @relayHash ec1db8377fbc781b81546fb0b4be5ba3
 */

/* eslint-disable */

'use strict';

/*::
import type { ConcreteRequest } from 'relay-runtime';
export type LocationTypeahead_LocationsQueryVariables = {|
  name: string
|};
export type LocationTypeahead_LocationsQueryResponse = {|
  +searchForEntity: {|
    +edges: ?$ReadOnlyArray<?{|
      +node: ?{|
        +entityId: string,
        +entityType: string,
        +name: string,
        +type: string,
      |}
    |}>
  |}
|};
export type LocationTypeahead_LocationsQuery = {|
  variables: LocationTypeahead_LocationsQueryVariables,
  response: LocationTypeahead_LocationsQueryResponse,
|};
*/


/*
query LocationTypeahead_LocationsQuery(
  $name: String!
) {
  searchForEntity(name: $name, first: 10) {
    edges {
      node {
        entityId
        entityType
        name
        type
      }
    }
  }
}
*/

const node/*: ConcreteRequest*/ = (function(){
var v0 = [
  {
    "kind": "LocalArgument",
    "name": "name",
    "type": "String!",
    "defaultValue": null
  }
],
v1 = [
  {
    "kind": "LinkedField",
    "alias": null,
    "name": "searchForEntity",
    "storageKey": null,
    "args": [
      {
        "kind": "Literal",
        "name": "first",
        "value": 10
      },
      {
        "kind": "Variable",
        "name": "name",
        "variableName": "name"
      }
    ],
    "concreteType": "SearchEntriesConnection",
    "plural": false,
    "selections": [
      {
        "kind": "LinkedField",
        "alias": null,
        "name": "edges",
        "storageKey": null,
        "args": null,
        "concreteType": "SearchEntryEdge",
        "plural": true,
        "selections": [
          {
            "kind": "LinkedField",
            "alias": null,
            "name": "node",
            "storageKey": null,
            "args": null,
            "concreteType": "SearchEntry",
            "plural": false,
            "selections": [
              {
                "kind": "ScalarField",
                "alias": null,
                "name": "entityId",
                "args": null,
                "storageKey": null
              },
              {
                "kind": "ScalarField",
                "alias": null,
                "name": "entityType",
                "args": null,
                "storageKey": null
              },
              {
                "kind": "ScalarField",
                "alias": null,
                "name": "name",
                "args": null,
                "storageKey": null
              },
              {
                "kind": "ScalarField",
                "alias": null,
                "name": "type",
                "args": null,
                "storageKey": null
              }
            ]
          }
        ]
      }
    ]
  }
];
return {
  "kind": "Request",
  "fragment": {
    "kind": "Fragment",
    "name": "LocationTypeahead_LocationsQuery",
    "type": "Query",
    "metadata": null,
    "argumentDefinitions": (v0/*: any*/),
    "selections": (v1/*: any*/)
  },
  "operation": {
    "kind": "Operation",
    "name": "LocationTypeahead_LocationsQuery",
    "argumentDefinitions": (v0/*: any*/),
    "selections": (v1/*: any*/)
  },
  "params": {
    "operationKind": "query",
    "name": "LocationTypeahead_LocationsQuery",
    "id": null,
    "text": "query LocationTypeahead_LocationsQuery(\n  $name: String!\n) {\n  searchForEntity(name: $name, first: 10) {\n    edges {\n      node {\n        entityId\n        entityType\n        name\n        type\n      }\n    }\n  }\n}\n",
    "metadata": {}
  }
};
})();
// prettier-ignore
(node/*: any*/).hash = 'b34a4b24708335635a362399d5a4c613';
module.exports = node;
