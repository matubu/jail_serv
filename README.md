# jail_serv
🚚 An experimental static web server using a chroot jail to prevent access to the rest of the filesystem

## Installation
```bash
git clone git@github.com:matubu/jail_serv.git
cd jail_serv
make
```

## Usage
```
Usage:
  serv [path] [...flags]

Options: 
  --port <port>  Specify the listening port.
  --host         Expose the server to external network.
```
