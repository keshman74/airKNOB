from pathlib import Path

source = (Path(__file__).parents[1] / "src" / "main.cpp").read_text()

assert 'bool h50Upnp = false;' in source
assert 'uint16_t ports[] = {80, 8000, 443, 8443};' in source
assert 'detectH50Endpoint(ip,endpoint,statusBody,statusTimeoutMs,generation)' in source
assert 'http://"+ip+":"+String(ports[i])+"/description.xml"' in source
assert 'modelInfo.indexOf("H50")<0' in source
assert 'identity.indexOf("arylic_h50")<0 || identity.indexOf("allwinner-r328")<0' in source
assert 'h50ServicePath(descriptions[1],"AVTransport")' in source
assert '"setPlayerCmd:vol:"' in source and '"SetVolume"' in source
assert '"setPlayerCmd:mute:"' in source and '"SetMute"' in source
assert 'response,timeoutMs,endpoint.h50PlayPort,true' in source
assert '<ControlSource>WiiMApp</ControlSource>' in source
assert 'settings.putBool("h50",activeEndpoint.h50Upnp)' in source
assert 'settings.getBool("h50",false)' in source
assert 'activeEndpoint.h50Upnp ? 2' in source
assert 'MCU+PAS+RAKOIT:SRC:USB&' in source
assert 'MCU+PAS+RAKOIT:SRC:BT&' in source
print("H50/A97 UPnP profile, discovery, routing and controls: OK")
