// <copyright file="WsaConnection.cs" company="MaaAssistantArknights">
// Part of the MaaWpfGui project, maintained by the MaaAssistantArknights team (Maa Team)
// Copyright (C) 2021-2026 MaaAssistantArknights Contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License v3.0 only as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY
// </copyright>

#nullable enable

using System;
using System.Globalization;
using System.Net;
using System.Runtime.InteropServices;

namespace MaaWpfGui.Helper;

/// <summary>
/// Detects a running Windows Subsystem for Android (WSA) instance and resolves its listening ADB address.
/// Supports both the default port (58526) and custom ADB ports exposed by WsaClient.exe.
/// </summary>
internal static class WsaConnection
{
    private const int DefaultWsaAdbPort = 58526;
    private const int AfInet = 2;
    private const int TcpTableOwnerPidListener = 3;

    public static bool IsWsaProcessName(string processName) =>
        processName.Equals("WsaClient", StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// Queries the IPv4 listening TCP port owned by WsaClient.exe (preferring 58526, otherwise the first high port).
    /// </summary>
    /// <param name="processId">The PID of WsaClient.exe.</param>
    /// <returns>127.0.0.1:&lt;port&gt; for ADB connection.</returns>
    public static string GetListeningAdbAddress(int processId)
    {
        var port = FindListeningPort(processId) ?? DefaultWsaAdbPort;
        return $"127.0.0.1:{port.ToString(CultureInfo.InvariantCulture)}";
    }

    private static int? FindListeningPort(int processId)
    {
        try
        {
            int bufferSize = 0;
            _ = GetExtendedTcpTable(IntPtr.Zero, ref bufferSize, false, AfInet, TcpTableOwnerPidListener, 0);
            if (bufferSize <= 0)
            {
                return null;
            }

            IntPtr buffer = Marshal.AllocHGlobal(bufferSize);
            try
            {
                if (GetExtendedTcpTable(buffer, ref bufferSize, false, AfInet, TcpTableOwnerPidListener, 0) != 0)
                {
                    return null;
                }

                int rowCount = Marshal.ReadInt32(buffer);
                int rowSize = Marshal.SizeOf<MibTcpRowOwnerPid>();
                IntPtr rowPtr = IntPtr.Add(buffer, sizeof(int));
                int? fallbackPort = null;

                for (int i = 0; i < rowCount; i++)
                {
                    var row = Marshal.PtrToStructure<MibTcpRowOwnerPid>(rowPtr);
                    rowPtr = IntPtr.Add(rowPtr, rowSize);

                    if (row.OwningPid != processId)
                    {
                        continue;
                    }

                    int port = (ushort)IPAddress.NetworkToHostOrder((short)row.LocalPort);
                    if (port == DefaultWsaAdbPort)
                    {
                        return port;
                    }

                    if (port >= 1024 && fallbackPort == null)
                    {
                        fallbackPort = port;
                    }
                }

                return fallbackPort;
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
        catch
        {
            return null;
        }
    }

    [DllImport("iphlpapi.dll", SetLastError = true)]
    private static extern uint GetExtendedTcpTable(
        IntPtr pTcpTable,
        ref int pdwSize,
        bool bOrder,
        int ulAf,
        int tableClass,
        uint reserved);

    [StructLayout(LayoutKind.Sequential)]
    private struct MibTcpRowOwnerPid
    {
        public uint State;
        public uint LocalAddr;
        public uint LocalPort;
        public uint RemoteAddr;
        public uint RemotePort;
        public int OwningPid;
    }
}
