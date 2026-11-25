/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import androidx.annotation.Nullable;
import java.util.Arrays;
import org.webrtc.PeerConnection;

/**
 * Representation of a single ICE Candidate, mirroring
 * {@code IceCandidate} in the C++ API.
 */
public class IceCandidate {
  public final String sdpMid;
  public final int sdpMLineIndex;
  public final String sdp;
  public final String serverUrl;
  public final PeerConnection.AdapterType adapterType;

  /**
   * The ICE username fragment (ufrag) of the ICE generation the candidate belongs to, as
   * {@code usernameFragment} in the web API. It changes with every ICE restart. Null when the
   * candidate does not name one.
   */
  @Nullable public final String usernameFragment;

  public IceCandidate(String sdpMid, int sdpMLineIndex, String sdp) {
    this.sdpMid = sdpMid;
    this.sdpMLineIndex = sdpMLineIndex;
    this.sdp = sdp;
    this.serverUrl = "";
    this.adapterType = PeerConnection.AdapterType.UNKNOWN;
    this.usernameFragment = usernameFragmentOf(sdp);
  }

  @CalledByNative
  IceCandidate(String sdpMid, int sdpMLineIndex, String sdp, String serverUrl,
      PeerConnection.AdapterType adapterType, String usernameFragment) {
    this.sdpMid = sdpMid;
    this.sdpMLineIndex = sdpMLineIndex;
    this.sdp = sdp;
    this.serverUrl = serverUrl;
    this.adapterType = adapterType;
    this.usernameFragment =
        usernameFragment == null || usernameFragment.isEmpty() ? null : usernameFragment;
  }

  /** Reads the fragment from a candidate line, which names it as {@code ufrag <value>}. */
  @Nullable
  private static String usernameFragmentOf(@Nullable String sdp) {
    if (sdp == null) {
      return null;
    }
    String[] tokens = sdp.trim().split("\\s+");
    for (int i = 0; i + 1 < tokens.length; i++) {
      if (tokens[i].equals("ufrag")) {
        return tokens[i + 1];
      }
    }
    return null;
  }

  @Override
  public String toString() {
    return sdpMid + ":" + sdpMLineIndex + ":" + sdp + ":" + serverUrl + ":"
        + adapterType.toString();
  }

  @CalledByNative
  String getSdpMid() {
    return sdpMid;
  }

  @CalledByNative
  String getSdp() {
    return sdp;
  }

  @CalledByNative
  int getSdpMLineIndex() {
    return sdpMLineIndex;
  }

  /** equals() checks sdpMid, sdpMLineIndex, and sdp for equality. */
  @Override
  public boolean equals(@Nullable Object object) {
    if (!(object instanceof IceCandidate)) {
      return false;
    }

    IceCandidate that = (IceCandidate) object;
    return objectEquals(this.sdpMid, that.sdpMid) && this.sdpMLineIndex == that.sdpMLineIndex
        && objectEquals(this.sdp, that.sdp);
  }

  @Override
  public int hashCode() {
    Object[] values = {sdpMid, sdpMLineIndex, sdp};
    return Arrays.hashCode(values);
  }

  private static boolean objectEquals(Object o1, Object o2) {
    if (o1 == null) {
      return o2 == null;
    }
    return o1.equals(o2);
  }
}
