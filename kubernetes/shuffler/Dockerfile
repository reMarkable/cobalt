# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

FROM cobalt-common

COPY shuffler /usr/local/bin/

ARG config_file
# This path must match the -config_file flag in shuffler-deployment.yaml
COPY ${config_file} /etc/cobalt/shuffler_config.txt

ENTRYPOINT ["shuffler"]
# This port number must match the one used in shuffler_deployment.yaml
EXPOSE 5000
