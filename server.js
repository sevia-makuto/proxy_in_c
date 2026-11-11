const net = require('net');

const server = net.createServer((socket) => {
    console.log('Client connected!');

    socket.on('data', (data) => {
        console.log('Client says:', data.toString());

        socket.write('Hello from the Node.js TCP server!\n');
    });

    socket.on('end', () => {
        console.log('Client disconnected');
    });
});

server.listen(8080, () => {
    console.log('Server listening on port 8080');
});