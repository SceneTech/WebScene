const channel = new MessageChannel();
channel.port1.onmessage = event => channel.port1.postMessage(event.data + 1);
postMessage(channel.port2, [channel.port2]);
