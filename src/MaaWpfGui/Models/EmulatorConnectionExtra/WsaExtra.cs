// <copyright file="WsaExtra.cs" company="MaaAssistantArknights">
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
using MaaWpfGui.Configuration.Factory;
using MaaWpfGui.Helper;
using Newtonsoft.Json.Linq;

namespace MaaWpfGui.Models.EmulatorConnectionExtra;

/// <summary>
/// WSA 窗口级高速截图与输入增强（复用 MaaWin32ControlUnit WGC FramePool，兼容 A11/A12L/A13/A16 WSA）。
/// </summary>
public class WsaExtra() : ExtraConfig
{
    /// <summary>
    /// Gets or sets a value indicating whether WSA window capture extras are enabled.
    /// </summary>
    public bool Enable
    {
        get; set {
            Instances.AsstProxy.Connected = false;
            ConfigFactory.CurrentConfig.Gui.ConnectSettings.Extras.Wsa.Enable = value;
            SetAndNotify(ref field, value);
        }
    } = ConfigFactory.CurrentConfig.Gui.ConnectSettings.Extras.Wsa.Enable;

    /// <summary>
    /// Gets or sets a value indicating whether Win32 host-side input is enabled for WSA.
    /// </summary>
    public bool EnableInput
    {
        get; set {
            Instances.AsstProxy.Connected = false;
            ConfigFactory.CurrentConfig.Gui.ConnectSettings.Extras.Wsa.EnableInput = value;
            SetAndNotify(ref field, value);
        }
    } = ConfigFactory.CurrentConfig.Gui.ConnectSettings.Extras.Wsa.EnableInput;

    /// <summary>
    /// Gets or sets an optional custom window title to match a specific WSA window.
    /// </summary>
    public string WindowName
    {
        get; set {
            var trimmed = value?.Trim() ?? string.Empty;
            Instances.AsstProxy.Connected = false;
            ConfigFactory.CurrentConfig.Gui.ConnectSettings.Extras.Wsa.WindowName = trimmed;
            SetAndNotify(ref field, trimmed);
        }
    } = ConfigFactory.CurrentConfig.Gui.ConnectSettings.Extras.Wsa.WindowName;

    public string Config =>
        Newtonsoft.Json.JsonConvert.SerializeObject(new JObject
        {
            ["enable"] = Enable,
            ["enable_input"] = EnableInput,
            ["window_name"] = WindowName,
        });
}
