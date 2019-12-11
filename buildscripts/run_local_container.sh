#python3 ./container_tester.py -v run_test --endpoint root@localhost:4000 --files  moduleconfig.py:/tmp/moduleconfig.py --script test1.sh

#python3 buildscripts/container_tester.py -v run_test --endpoint root@localhost:4000 --files mongod:/root/mongod  mongo:/root/mongo --script test1.sh

#python3 buildscripts/container_tester.py -v run_test --endpoint root@3.15.149.114 --files mongod:/root/mongod  mongo:/root/mongo /home/mark/src/mongo/src/mongo/db/modules/enterprise/jstests/external_auth/lib/ecs_hosted_test.js:/root/ecs_hosted_test.js --script /home/mark/src/mongo/src/mongo/db/modules/enterprise/jstests/external_auth/lib/ecs_hosted_test.sh

# Test
# Copy
# MongoD
# Mongo
# A test js file
# A Test shell script
# Run test shell script

