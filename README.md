# Magma

[![Build Status](https://travis-ci.com/facebookincubator/magma.svg?branch=master)](https://travis-ci.com/facebookincubator/magma)

Magma is an open-source software platform that gives network operators an open, flexible and extendable mobile core network solution. Magma enables better connectivity by:

* Allowing operators to offer cellular service without vendor lock-in with a modern, open source core network
* Enabling op   erators to manag  e their networks more effici   ently with more automation, less downtime, better predictability, and more agility to add new services and applications
* Enabling federation between existing MNOs and new infrastructure providers for expanding rural infrastructure
* Allowing operators who are constrained with licensed s  pectrum to add capacity and reach by using Wi-Fi and CBRS


## Magma Architecture

The figure below shows the high-level Magma archite   cture. Magma is 3GPP generation (2G, 3G, 4G or upcoming 5G networks) and access network agnostic (cellular or WiFi). It can flexibly support a radio access network with minimal development and deployment effort.

Magma has three major components:

* **Access Gateway:** The Access Gateway (AGW) provides network services and policy enforcement. In an LTE network, the AGW implements an evolved packet core (EPC), and a combination of an AAA and a PGW. It works with existing, unmodified commercial radio hardware.

* **Orchestrator:** Orchestrator is a cloud service that provides a simple and consistent way to configure and monitor the wireless network securely. The Orchestrator can be hosted on a public/private cloud. The metrics acquired through the platform allows you to see the analytics and traffic flows of the wireless users through the Magma web UI.

* **Federation Gateway:** The Federation Gateway integrates the MNO core network with Magma by using standard 3GPP interfaces to existing MNO components.  It acts as a proxy between the Magma AGW and the operator's network and facilitates core functions, such as authentication, data plans, policy enforcement, and charging to stay uniform between an existing MNO network and the expanded network with Magma.

![Magma architecture diagram](docs/readmes/assets/magma_overview.png?raw=true "Magma Architecture")

## Usage Docs
The documentation for developing and using Magma is avaliable at: [https://facebookincubator.github.io/magma](https://facebookincubator.github.io/magma)

## Join the Magma Community

- Mailing lists:
  - Join [magma-dev](https://groups.google.com/forum/#!forum/magma-dev) for technical discussions
  - Join [magma-announce](https://groups.google.com/forum/#!forum/magma-announce) for announcements
- Discord:
  - [magma\_dev](https://discord.gg/WDBpebF) channel

See the [CONTRIBUTING](CONTRIBUTING.md) file for how to help out.

## License

Magma is BSD License licensed, as found in the LICENSE file.
The EPC is OAI is offered under the OAI Apache 2.0 license, as found in the LICENSE file in the OAI directory.
