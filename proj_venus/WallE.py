import zmq
from heat_map import venusMap
import time
import math
from math import hypot
import random

class Droid:
    def __init__(self, start, direction, venusMap, name="WallE"):
        self.name = name
        self.Pos = [float(start[0]), float(start[1])]
        self.direction = direction
        self.state = 'scanning'
        self.map = venusMap
        self.speed     = 0.0 
        self.max_speed = 0.4
        self.robot_radius = 7.25

        self.scan_angle_swept = 0   
        self.finding_edge_angle = 0
        self.locked_target  = None  
        self.estimated_cube_pos = None
        self.lock_grace = 0    

        self.blindfold = 0 
        self.pause_timer = 0
        
        self.measure_angle = 0
        self.measure_dists = []

        self.measure_start_point = [0.0, 0.0]
        self.measure_end_point = [0.0, 0.0]
        self.lock_turn_remaining = 0
        
        self.path = []
        self.path_idx = 0
        self.sweep_target = None
        
        self.dodge_target = [0.0, 0.0]
        self.state_after_reverse = 'scanning'

        self.checkpoints = []
        self.scanning = False

        self.turn_angle = 0

    @property
    def data(self):
        return {
            'position'  : self.Pos,
            'direction' : self.direction,
            'obstacles' : list(self.map.obstacles),
            'hits'      : self.map.bingo,
            'state'     : self.state,
            'speed'     : self.speed,
            'scanning'  : self.scanning,
            'turn_angle': self.turn_angle
        }

    def rotate_dir(self, deg):
        self.turn_angle = math.radians(deg)
        t  = math.radians(deg)
        nx = self.direction[0]*math.cos(t) - self.direction[1]*math.sin(t)
        ny = self.direction[0]*math.sin(t) + self.direction[1]*math.cos(t)
        m  = hypot(nx, ny)
        self.direction = [nx/m, ny/m]

    def turn_towards_point(self, tx, ty):
        dx, dy = self.direction
        vx, vy = tx - self.Pos[0], ty - self.Pos[1]
        dist = hypot(vx, vy)
        if dist == 0: return True
        vx, vy = vx/dist, vy/dist
        
        dot = dx*vx + dy*vy
        cross = dx*vy - dy*vx
        angle = math.degrees(math.atan2(cross, dot))
        
        if abs(angle) <= 1.0:
            self.direction = [vx, vy]
            return True
        else:
            self.rotate_dir(1.0 if angle > 0 else -1.0)
            return False

    def should_scan(self):
        for cp in self.checkpoints:
            if hypot(self.Pos[0] - cp[0], self.Pos[1] - cp[1]) < 80:
                return False
        return True

    def enter_reverse(self, next_state):
        self.speed = -(self.max_speed / 2.0)
        self.state = 'reversing'
        self.dodge_target = [self.Pos[0] - self.direction[0] * 5.0, self.Pos[1] - self.direction[1] * 5.0]
        self.blindfold = 15 
        self.state_after_reverse = next_state

    def update(self, long_dist, hit_fatal):
        self.map.claim(self.Pos)

        if self.pause_timer > 0:
            self.pause_timer -= 1
            self.speed = 0.0 
            return

        if self.blindfold > 0:
            self.blindfold -= 1
            hit_fatal = False 

        if self.state in ['reversing', 'avoid_friend_turn']:
            hit_fatal = False

        seeing_object = (long_dist >= 4 and long_dist <= 80)
        hit_pos = [self.Pos[0] + self.direction[0]*(long_dist + self.robot_radius), 
                self.Pos[1] + self.direction[1]*(long_dist + self.robot_radius)] if seeing_object else None
        
        if seeing_object and any(hypot(hit_pos[0]-c[0], hit_pos[1]-c[1]) < 15 for c in self.map.bingo):
            seeing_object = False

        if seeing_object and any(hypot(hit_pos[0]-p[0], hit_pos[1]-p[1]) < 15 and p[2] > 0
                                 for p in self.map.pending_cubes):
            seeing_object = False

        if seeing_object and any(hypot(hit_pos[0]-p[0], hit_pos[1]-p[1]) < 15 and p[2] > 0
            for p in self.map.pending_mountains):
                seeing_object = False

        if seeing_object:
            for clanker in self.map.clankers:
                if clanker == self.Pos: continue
                if hypot(hit_pos[0]-clanker[0], hit_pos[1]-clanker[1]) < 20:
                    seeing_object = False
                    break

        if hit_fatal:
            bump_pos = [self.Pos[0] + self.direction[0]* (self.robot_radius + 2), 
                        self.Pos[1] + self.direction[1]* (self.robot_radius + 2)]
            hit_friend = False
            for clanker in self.map.clankers:
                if clanker == self.Pos: continue
                if hypot(self.Pos[0]-clanker[0], self.Pos[1]-clanker[1]) < (self.robot_radius * 2 + 2): 
                    hit_friend = True
                    break
                    
            if hit_friend:
                print(f"[{self.name}] Collision Alert! Bumped into friend. Reversing.")
                self.enter_reverse('avoid_friend_turn')
                self.pause_timer = random.randint(15, 30)
                return
            
            print(f"[{self.name}] Collision Alert! Hit an obstacle near {bump_pos}. Reversing.")
            
            if self.state == 'locked':
                if self.estimated_cube_pos:
                    self.map.pending_cubes.append([
                        self.estimated_cube_pos[0],
                        self.estimated_cube_pos[1],
                        200
                    ])
                    print(f"[{self.name}] Hit obstacle while locked! Cube saved as pending.")
                self.locked_target = None
                
            self.map.mark_obstacle(bump_pos)
            next_state = 'scanning' if self.should_scan() else 'planning_sweep'
            self.enter_reverse(next_state)
            self.pause_timer = 15
            return

        if self.state == 'scanning':
            self.speed = 0.0
            self.checkpoints.append(list(self.Pos)) 
            if seeing_object:
                print(f"[{self.name}] Object detected. Finding edge.")
                self.state = 'finding_edge'
                self.finding_edge_angle = 0
            else:
                self.rotate_dir(1) 
                self.scan_angle_swept += 1
                if self.scan_angle_swept >= 360:
                    print(f"[{self.name}] 360-degree scan complete. Assessing map.")
                    self.scan_angle_swept = 0
                    self.checkpoints.append(list(self.Pos)) 
                    self.state = 'planning_sweep'

        elif self.state == 'finding_edge':
            self.speed = 0.0
            self.rotate_dir(-1)
            self.finding_edge_angle += 1
            if not seeing_object:
                self.state = 'measuring'
                self.measure_angle = 0
                self.measure_dists = []
            elif self.finding_edge_angle > 360:
                print(f"[{self.name}] Surrounded or invalid edge! Aborting scan.")
                self.state = 'scanning' if self.should_scan() else 'planning_sweep'

        elif self.state == 'measuring':
            self.speed = 0.0
            self.rotate_dir(1)
            self.measure_angle += 1
            
            if seeing_object:
                if len(self.measure_dists) == 0:
                    self.measure_dists.append(long_dist)
                    hit_x = self.Pos[0] + self.direction[0] * (long_dist + self.robot_radius)
                    hit_y = self.Pos[1] + self.direction[1] * (long_dist + self.robot_radius)
                    self.measure_start_point = [hit_x, hit_y]
                    self.measure_end_point = [hit_x, hit_y]
                else:
                    if abs(long_dist - self.measure_dists[-1]) > 15:
                        seeing_object = False
                    else:
                        self.measure_dists.append(long_dist)
                        hit_x = self.Pos[0] + self.direction[0] * (long_dist + self.robot_radius)
                        hit_y = self.Pos[1] + self.direction[1] * (long_dist + self.robot_radius)
                        self.measure_end_point = [hit_x, hit_y]
                        self.map.mark_obstacle([hit_x, hit_y])
                        
                        if len(self.measure_dists) > 120: 
                            print(f"[{self.name}] Classification: MOUNTAIN (Too massive). Replanning.")
                            self.map.known[self.map.gridLock(self.measure_start_point)] = 1
                            self.map.known[self.map.gridLock(self.measure_end_point)] = 1
                            self.map.pending_mountains.append([
                                (self.measure_start_point[0] + self.measure_end_point[0]) / 2,
                                (self.measure_start_point[1] + self.measure_end_point[1]) / 2,
                                150
                            ])
                            self.state = 'planning_sweep'
            
            if not seeing_object:
                if len(self.measure_dists) > 0:
                    dx = self.measure_end_point[0] - self.measure_start_point[0]
                    dy = self.measure_end_point[1] - self.measure_start_point[1]
                    chord_length = hypot(dx, dy)
                    
                    print(f"\n--- [{self.name}] Object Measured ---")
                    print(f"Chord Width: {chord_length:.2f} cm")
                    
                    is_friend = False
                    for clanker in self.map.clankers:
                        if clanker == self.Pos: continue
                        if hypot(self.measure_start_point[0]-clanker[0], self.measure_start_point[1]-clanker[1]) < 20 or \
                        hypot(self.measure_end_point[0]-clanker[0], self.measure_end_point[1]-clanker[1]) < 20:
                            is_friend = True
                            break
                            
                    estimated_pos = [(self.measure_start_point[0] + self.measure_end_point[0])/2.0,
                                    (self.measure_start_point[1] + self.measure_end_point[1])/2.0]
                                    
                    is_already_found = any(hypot(estimated_pos[0]-c[0], estimated_pos[1]-c[1]) < 15 for c in self.map.bingo)
                    is_on_cooldown   = any(hypot(estimated_pos[0]-p[0], estimated_pos[1]-p[1]) < 15 and p[2] > 0
                                          for p in self.map.pending_cubes)

                    if is_friend:
                        print(f"[{self.name}] Classification: FRIEND (Replanning)")
                        self.state = 'planning_sweep'
                    elif is_already_found:
                        print(f"[{self.name}] Classification: ALREADY FOUND CUBE (Replanning)")
                        self.state = 'planning_sweep'
                    elif is_on_cooldown:
                        print(f"[{self.name}] Classification: PENDING CUBE on cooldown (Replanning)")
                        self.state = 'planning_sweep'
                    elif 2.0 <= chord_length <= 15.0 and len(self.measure_dists) < 80: 
                        print(f"[{self.name}] Classification: CUBE (Turning back to lock!)\n")
                        self.state = 'locking_turn'
                        self.lock_turn_remaining = len(self.measure_dists) / 2.0
                        self.estimated_cube_pos = estimated_pos
                    else:
                        print(f"[{self.name}] Classification: MOUNTAIN/NOISE (Replanning)\n")
                        self.map.known[self.map.gridLock(estimated_pos)] = 1
                        self.map.pending_mountains.append([estimated_pos[0], estimated_pos[1], 150])
                        self.state = 'planning_sweep'
                else:
                    if self.measure_angle > 360:
                        self.state = 'scanning' if self.should_scan() else 'planning_sweep'

        elif self.state == 'locking_turn':
            self.speed = 0.0
            self.rotate_dir(-1)
            self.lock_turn_remaining -= 1
            if self.lock_turn_remaining <= 0:
                print(f"[{self.name}] Target locked! Full speed ahead.")
                self.state = 'locked'
                self.lock_grace = 0

        elif self.state == 'locked':
            if seeing_object:
                self.speed = self.max_speed
                self.lock_grace = 0
                if long_dist <= 8:
                    hit_pos_exact = [self.Pos[0] + self.direction[0]* (long_dist + self.robot_radius), 
                                    self.Pos[1] + self.direction[1]* (long_dist + self.robot_radius)]
                    print(f"[{self.name}] Proximity capture! Collecting.")
                    self.map.found_cubes(hit_pos_exact)
                    self.map.mark_obstacle(hit_pos_exact)
                    self.locked_target = None
                    self.estimated_cube_pos = None
                    self.enter_reverse('scanning' if self.should_scan() else 'planning_sweep')
                    self.scanning = not self.scanning
                    self.pause_timer = 15
                    return
            else:
                self.speed = 0.0
                self.lock_grace += 1
                if self.lock_grace < 12:
                    self.rotate_dir(1)
                elif self.lock_grace < 36:
                    self.rotate_dir(-1)
                elif self.lock_grace < 60:
                    self.rotate_dir(1)
                else:
                    print(f"[{self.name}] Target lost completely. Replanning.")
                    self.estimated_cube_pos = None
                    self.state = 'scanning' if self.should_scan() else 'planning_sweep'
            
        elif self.state == 'planning_sweep':
            self.speed = 0.0
            target = self.map.find_nearest_unknown(self.Pos)
            if target:
                path = self.map.astar(self.Pos, target)
                if path:
                    print(f"[{self.name}] Path found to unexplored sector {target}. Commencing sweep.")
                    self.path = path
                    self.path_idx = 0
                    self.state = 'sweeping'
                    self.sweep_target = target
                else:
                    print(f"[{self.name}] Sector {target} unreachable. Marking as claimed.")
                    self.map.known[self.map.gridLock(target)] = 1
                    self.pause_timer = 10
            else:
                print(f"[{self.name}] Map fully explored. Holding position.")
                self.pause_timer = 120

        elif self.state == 'sweeping':
            if seeing_object:
                print(f"[{self.name}] Sweep interrupted by object. Finding edge.")
                self.state = 'finding_edge'
                self.finding_edge_angle = 0
                self.speed = 0.0
                return
                
            if self.should_scan():
                print(f"[{self.name}] Reached new vantage point. Stopping to scan.")
                self.state = 'scanning'
                self.scan_angle_swept = 0
                self.speed = 0.0
                return
                
            if self.path_idx >= len(self.path):
                print(f"[{self.name}] Reached end of planned path. Replanning.")
                self.state = 'scanning' if self.should_scan() else 'planning_sweep'
                self.speed = 0.0
                return
                
            wp = self.path[self.path_idx]
            dist_wp = hypot(wp[0] - self.Pos[0], wp[1] - self.Pos[1])
            
            if dist_wp < self.max_speed * 2:
                self.path_idx += 1
            else:
                if self.turn_towards_point(wp[0], wp[1]):
                    self.speed = self.max_speed
                else:
                    self.speed = 0.0

        elif self.state == 'reversing':
            dx = self.dodge_target[0] - self.Pos[0]
            dy = self.dodge_target[1] - self.Pos[1]
            dist_r = hypot(dx, dy)
            if dist_r <= abs(self.speed):
                print(f"[{self.name}] Reverse complete. Transitioning to {self.state_after_reverse}.")
                self.speed = 0.0
                self.state = self.state_after_reverse
                self.scan_angle_swept = 0
            else:
                self.speed = -(self.max_speed / 2.0)

        elif self.state == 'avoid_friend_turn':
            self.speed = 0.0
            self.pause_timer = random.randint(10, 25)
            self.state = 'planning_sweep'

        self.Pos[0] += self.speed * self.direction[0]
        self.Pos[1] += self.speed * self.direction[1]

def main():
    ctx = zmq.Context()
    WallE_loc = ctx.socket(zmq.PUB)
    WallE_loc.bind("ipc:///home/student/proj_venus/WallE_info.ipc")

    sensors = ctx.socket(zmq.SUB)
    sensors.connect("ipc:///home/student/proj_venus/sensors.ipc")
    sensors.set(zmq.SUBSCRIBE, b"")

    # R2_loc = ctx.socket(zmq.SUB)
    # R2_loc.setsockopt(zmq.CONFLATE, 1)
    # R2_loc.connect("ipc://R2_info")      
    # R2_loc.set(zmq.SUBSCRIBE, b"")

    venus_map = venusMap()
    WallE     = Droid([0, 0], [0, -1], venus_map, name="WallE")

    print("[WallE] Systems initialized. WallE is ready.")
    while True:
        try:
            msg = sensors.recv_json(zmq.NOBLOCK)
            if 'distance' in msg: break
        except zmq.Again:
            time.sleep(0.05)
    print("[WallE] Deployment successful. T0 for WallE!")

    while True:
        venus_map.clankers = [WallE.Pos]
        venus_map.tick_pending()

        # try:
        #     r2 = R2_loc.recv_json(zmq.NOBLOCK)
        #     WallE.map.claim(r2['position'])
        #     venus_map.clankers.append(r2['position'])
        #     for obs in r2['obstacles']: WallE.map.obstacles.add(tuple(obs))
        # except zmq.Again:
        #     pass

        long_dist = 0
        hit = False
        valid = False

        while True:
            try:
                msg = sensors.recv_json(zmq.NOBLOCK)
                if 'tape' in msg: 
                    valid = True
                    long_dist = msg.get('distance', 999)
                    hit = hit or msg['tape']
            except zmq.Again:
                break

        if valid:
            WallE.update(long_dist, hit)

        WallE_loc.send_json(WallE.data)
        time.sleep(0.016)

if __name__ == '__main__':
    main()