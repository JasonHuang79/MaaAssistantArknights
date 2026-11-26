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
namespace MaaWpfGui.Configuration.Single.Settings.ConnectionExtra;

public class WsaExtra : BaseExtra
{
    /// <summary>
    /// Gets or sets a value indicating whether to enable WSA window-level WGC screencap extras.
    /// </summary>
    public bool Enable { get; set; } = true;

    /// <summary>
    /// Gets or sets a value indicating whether to enable Win32 host-side input for WSA (experimental).
    /// </summary>
    public bool EnableInput { get; set; }

    /// <summary>
    /// Gets or sets an optional custom WSA window title override. Leave empty for automatic package/AUMID matching.
    /// </summary>
    public string WindowName { get; set; } = string.Empty;
}
