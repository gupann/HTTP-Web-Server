### Build/test container ###
# Define builder stage
FROM wasd-gamers:base AS builder

# Share work directory
COPY . /usr/src/project
WORKDIR /usr/src/project/build_coverage

# Build and test
RUN cmake -DCMAKE_BUILD_TYPE=Coverage ..
RUN make coverage

# Final stage
CMD ["true"]