"""
Copyright (c) 2016-present, Facebook, Inc.
All rights reserved.

This source code is licensed under the BSD-style license found in the
LICENSE file in the root directory of this source tree. An additional grant
of patent rights can be found in the PATENTS file in the same directory.
"""
import netifaces
from collections import namedtuple

from magma.common.misc_utils import cidr_to_ip_netmask_tuple
from magma.pipelined.app.base import MagmaController, ControllerType
from magma.pipelined.openflow import flows
from magma.pipelined.openflow.magma_match import MagmaMatch
from magma.pipelined.openflow.registers import Direction, load_passthrough

from ryu.controller import dpset
from ryu.lib.packet import ether_types, arp


class ArpController(MagmaController):
    """
    ArpController acts as an ARP responder for ARP requests to UE IP blocks.
    The following flow rules are installed on table 2 on switch connect, by
    order of priority:

    1. ARP responder for all ARP requests to UE IP blocks which constructs an
    ARP packet with source hardware address as the MAC of the virtual
    interface.

    2. On all outgoing IP packets from GTP, fill in eth_dst field of the packet
    with MAC address of the default gateway.
    """
    APP_NAME = 'arpd'
    APP_TYPE = ControllerType.PHYSICAL
    FLOW_PUSH_INTERVAL_SECS = 15

    # Inherited from app_manager.RyuApp
    _CONTEXTS = {
        'dpset': dpset.DPSet,
    }

    ArpdConfig = namedtuple(
        'ArpdConfig',
        ['virtual_iface', 'virtual_mac', 'ue_ip_blocks'],
    )

    def __init__(self, *args, **kwargs):
        super(ArpController, self).__init__(*args, **kwargs)
        self.table_num = self._service_manager.get_table_num(self.APP_NAME)
        self.next_table = self._service_manager.get_next_table_num(
            self.APP_NAME)
        self.dpset = kwargs['dpset']  # type: dpset.DPSet
        self.local_eth_addr = kwargs['config']['local_ue_eth_addr']
        self.setup_type = kwargs['config']['setup_type']
        self.allow_unknown_uplink_arps = kwargs['config']['allow_unknown_arps']
        if self.local_eth_addr:
            self.config = self._get_config(kwargs['config'], kwargs['mconfig'])
        self._current_ues = []

    def _get_config(self, config_dict, mconfig):
        def get_virtual_iface_mac():
            virtual_iface = config_dict['virtual_interface']
            virt_ifaddresses = netifaces.ifaddresses(virtual_iface)
            return virt_ifaddresses[netifaces.AF_LINK][0]['addr']

        return self.ArpdConfig(
            virtual_iface=config_dict['virtual_interface'],
            virtual_mac=get_virtual_iface_mac(),
            ue_ip_blocks=[cidr_to_ip_netmask_tuple(mconfig.ue_ip_block)],
        )

    def initialize_on_connect(self, datapath):
        flows.delete_all_flows_from_table(datapath, self.table_num)
        if self.local_eth_addr:
            for ip_block in self.config.ue_ip_blocks:
                self.add_ue_arp_flows(datapath, ip_block,
                                       self.config.virtual_mac)
            self._install_default_eth_dst_flow(datapath)
        if self.setup_type == 'CWF' and self.allow_unknown_uplink_arps:
            self._install_allow_incoming_arp_flow(datapath)

        self._install_default_forward_flow(datapath)
        self._install_default_arp_drop_flow(datapath)

    def add_ue_arp_flows(self, datapath, ue_ip, ue_mac):
        """
        Installs flows to allow arp traffic from the UE and to reply to ARPs
        sent for the UE ip address
        """
        self._set_incoming_arp_flows(datapath, ue_ip, ue_mac)
        # If we already installed an outgoing allow don't overwrite the rule
        # TODO its probably better for ue mac to manage this
        if ue_ip not in self._current_ues:
            self._current_ues.append(ue_ip)
            self._set_outgoing_arp_flows(datapath, ue_ip)

    def cleanup_on_discnnect(self, datapath):
        self.delete_all_flows(datapath)
        self._set_incoming_arp_flows(datapath)
        self._set_default_eth_dst_flow(datapath)
        self._set_default_forward_flow(datapath)

    def cleanup_on_disconnect(self, datapath):
        self.delete_all_flows(datapath)

    def delete_all_flows(self, datapath):
        flows.delete_all_flows_from_table(datapath, self.table_num)

    def _set_incoming_arp_flows(self, datapath, ip_block, src_mac):
        """
        Install flow rules for incoming ARPs(to UE):
            - For ARP request: respond to incoming ARP requests.
            - For ARP response: pass to next table.
        """
        parser = datapath.ofproto_parser
        ofproto = datapath.ofproto

        arp_resp_match = MagmaMatch(eth_type=ether_types.ETH_TYPE_ARP,
                                    direction=Direction.IN,
                                    arp_op=arp.ARP_REPLY, arp_tpa=ip_block)
        # Set so packet skips enforcement and send to egress
        actions = [load_passthrough(parser)]

        flows.add_resubmit_next_service_flow(datapath, self.table_num,
                                             arp_resp_match, actions=actions,
                                             priority=flows.UE_FLOW_PRIORITY,
                                             resubmit_table=self.next_table)

        # Set up ARP responder using flow rules. Add a rule with the following
        # 1. eth_dst becomes eth_src (back to sender)
        # 2. eth_src becomes the bridge MAC
        # 3. Set ARP op field to reply
        # 4. Target MAC becomes source MAC
        # 5. Source MAC becomes bridge MAC
        # 6. Swap target and source IPs using register 0 as a buffer
        # 7. Send back to the port the packet came on
        arp_req_match = MagmaMatch(eth_type=ether_types.ETH_TYPE_ARP,
                                   direction=Direction.IN,
                                   arp_op=arp.ARP_REQUEST, arp_tpa=ip_block)
        actions = [
            parser.NXActionRegMove(src_field='eth_src',
                                   dst_field='eth_dst',
                                   n_bits=48),
            parser.OFPActionSetField(eth_src=src_mac),
            parser.OFPActionSetField(arp_op=arp.ARP_REPLY),
            parser.NXActionRegMove(src_field='arp_sha',
                                   dst_field='arp_tha',
                                   n_bits=48),
            parser.OFPActionSetField(arp_sha=src_mac),
            parser.NXActionRegMove(src_field='arp_tpa',
                                   dst_field='reg0',
                                   n_bits=32),
            parser.NXActionRegMove(src_field='arp_spa',
                                   dst_field='arp_tpa',
                                   n_bits=32),
            parser.NXActionRegMove(src_field='reg0',
                                   dst_field='arp_spa',
                                   n_bits=32),
        ]
        flows.add_output_flow(datapath, self.table_num, arp_req_match, actions,
                              priority=flows.UE_FLOW_PRIORITY,
                              output_port=ofproto.OFPP_IN_PORT)

    def _set_outgoing_arp_flows(self, datapath, ip_block):
        """
        Install a flow rule to allow any ARP packets coming from the UE
        """
        parser = datapath.ofproto_parser
        match = MagmaMatch(eth_type=ether_types.ETH_TYPE_ARP,
                           direction=Direction.OUT,
                           arp_spa=ip_block)
        # Set so packet skips enforcement and send to egress
        actions = [load_passthrough(parser)]

        flows.add_resubmit_next_service_flow(datapath, self.table_num, match,
                                             actions=actions,
                                             priority=flows.UE_FLOW_PRIORITY,
                                             resubmit_table=self.next_table)

    def _install_default_arp_drop_flow(self, datapath):
        """
        Install default drop flow for all unmatched arps
        """
        # Drop all other ARPs
        match = MagmaMatch(eth_type=ether_types.ETH_TYPE_ARP)
        flows.add_drop_flow(datapath, self.table_num, match, [],
                            priority=flows.DEFAULT_PRIORITY)

    def _install_default_eth_dst_flow(self, datapath):
        """
        Add lower-pri flow rule to set `eth_dst` on outgoing packets to the
        specified MAC address.
        """
        self.logger.info('Setting default eth_dst to %s',
                         self.config.virtual_iface)
        parser = datapath.ofproto_parser
        match = MagmaMatch(eth_type=ether_types.ETH_TYPE_IP,
                           direction=Direction.OUT)
        actions = [
            parser.NXActionRegLoad2(dst='eth_dst', value=self.config.virtual_mac),
        ]
        flows.add_resubmit_next_service_flow(datapath, self.table_num, match,
                                             actions,
                                             priority=flows.DEFAULT_PRIORITY,
                                             resubmit_table=self.next_table)

    def _install_default_forward_flow(self, datapath):
        """
        Set a default 0-priority flow to forward to the next table.
        """
        match = MagmaMatch()
        flows.add_resubmit_next_service_flow(datapath, self.table_num, match,
                                             [],
                                             priority=flows.MINIMUM_PRIORITY,
                                             resubmit_table=self.next_table)

    def _install_allow_incoming_arp_flow(self, datapath):
        """
        Install a flow rule to allow any ARP packets coming from the UPLINK,
        this will be hit if arp clamping doesn't recognize the address
        """
        parser = datapath.ofproto_parser
        match = MagmaMatch(eth_type=ether_types.ETH_TYPE_ARP,
                           direction=Direction.IN)
        # Set so packet skips enforcement and send to egress
        actions = [load_passthrough(parser)]

        flows.add_resubmit_next_service_flow(datapath, self.table_num, match,
            actions=actions, priority=flows.UE_FLOW_PRIORITY - 1,
            resubmit_table=self.next_table)
