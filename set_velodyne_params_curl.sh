# curl http://192.168.0.201/cgi/diag.json
# sleep 1
# curl http://192.168.0.201/cgi/status.json
# sleep 1
curl --data "rpm=600" http://192.168.1.201/cgi/setting
sleep 5
curl http://192.168.1.201/cgi/status.json