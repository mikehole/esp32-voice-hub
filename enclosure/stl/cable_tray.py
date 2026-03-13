#!/usr/bin/env python3
"""
Generate a cable management tray for the ESP32 Voice Hub stand.
The stand drops DOWN into the box and rests on an internal ledge near the top.
Cables are hidden underneath. USB-A holes on left, right, and back walls.
"""

import numpy as np
from stl import mesh

# Dimensions (mm)
STAND_WIDTH = 98.0    # X dimension of stand base
STAND_DEPTH = 187.0   # Y dimension of stand base

WALL_THICKNESS = 2.5
LEDGE_WIDTH = 5.0     # Width of the internal ledge the stand sits on
LEDGE_FROM_TOP = 3.0  # How far down from the top the ledge is (stand drops in this much)
TRAY_HEIGHT = 30.0    # Total height of the box

# Outer dimensions - walls go around the stand
OUTER_WIDTH = STAND_WIDTH + (WALL_THICKNESS * 2)
OUTER_DEPTH = STAND_DEPTH + (WALL_THICKNESS * 2)

# USB-A hole dimensions (slightly oversized for tolerance)
USB_WIDTH = 15.0
USB_HEIGHT = 8.0
USB_HOLE_BOTTOM = 8.0  # Height from tray bottom to hole bottom

def create_box(x, y, z, cx=0, cy=0, cz=0):
    """Create a box mesh centered at (cx, cy, cz) with dimensions (x, y, z)."""
    vertices = np.array([
        [cx - x/2, cy - y/2, cz - z/2],  # 0
        [cx + x/2, cy - y/2, cz - z/2],  # 1
        [cx + x/2, cy + y/2, cz - z/2],  # 2
        [cx - x/2, cy + y/2, cz - z/2],  # 3
        [cx - x/2, cy - y/2, cz + z/2],  # 4
        [cx + x/2, cy - y/2, cz + z/2],  # 5
        [cx + x/2, cy + y/2, cz + z/2],  # 6
        [cx - x/2, cy + y/2, cz + z/2],  # 7
    ])
    
    faces = np.array([
        [0, 3, 1], [1, 3, 2],  # bottom
        [4, 5, 7], [5, 6, 7],  # top
        [0, 1, 4], [1, 5, 4],  # front
        [2, 3, 6], [3, 7, 6],  # back
        [0, 4, 3], [3, 4, 7],  # left
        [1, 2, 5], [2, 6, 5],  # right
    ])
    
    box = mesh.Mesh(np.zeros(faces.shape[0], dtype=mesh.Mesh.dtype))
    for i, face in enumerate(faces):
        for j in range(3):
            box.vectors[i][j] = vertices[face[j]]
    return box

def create_tray():
    """Create the cable tray - an open-top box with internal ledge for the stand."""
    meshes = []
    
    # The ledge is near the top - stand rests on it
    ledge_z = TRAY_HEIGHT - LEDGE_FROM_TOP - WALL_THICKNESS  # Top surface of ledge
    
    # Hole positions (in the lower portion of walls)
    hole_bottom_z = WALL_THICKNESS + USB_HOLE_BOTTOM
    hole_top_z = hole_bottom_z + USB_HEIGHT
    
    # === BOTTOM ===
    floor = create_box(OUTER_WIDTH, OUTER_DEPTH, WALL_THICKNESS,
                       cz=WALL_THICKNESS/2)
    meshes.append(floor)
    
    # === WALLS ===
    # Walls go full height. USB holes are cut into the lower portion.
    
    wall_height = TRAY_HEIGHT - WALL_THICKNESS
    inner_width = OUTER_WIDTH - 2 * WALL_THICKNESS
    inner_depth = OUTER_DEPTH - 2 * WALL_THICKNESS
    
    # --- FRONT WALL (solid - no USB hole) ---
    front_wall = create_box(OUTER_WIDTH, WALL_THICKNESS, wall_height,
                            cy=-OUTER_DEPTH/2 + WALL_THICKNESS/2,
                            cz=WALL_THICKNESS + wall_height/2)
    meshes.append(front_wall)
    
    # --- BACK WALL with USB hole ---
    back_y = OUTER_DEPTH/2 - WALL_THICKNESS/2
    
    # Bottom section (below hole)
    back_bottom = create_box(OUTER_WIDTH, WALL_THICKNESS, USB_HOLE_BOTTOM,
                             cy=back_y,
                             cz=WALL_THICKNESS + USB_HOLE_BOTTOM/2)
    meshes.append(back_bottom)
    
    # Top section (above hole)
    top_section_height = wall_height - USB_HOLE_BOTTOM - USB_HEIGHT
    if top_section_height > 0:
        back_top = create_box(OUTER_WIDTH, WALL_THICKNESS, top_section_height,
                              cy=back_y,
                              cz=hole_top_z + top_section_height/2)
        meshes.append(back_top)
    
    # Left of hole
    side_section_width = (OUTER_WIDTH - USB_WIDTH) / 2
    back_left = create_box(side_section_width, WALL_THICKNESS, USB_HEIGHT,
                           cx=-OUTER_WIDTH/2 + side_section_width/2,
                           cy=back_y,
                           cz=hole_bottom_z + USB_HEIGHT/2)
    meshes.append(back_left)
    
    # Right of hole
    back_right = create_box(side_section_width, WALL_THICKNESS, USB_HEIGHT,
                            cx=OUTER_WIDTH/2 - side_section_width/2,
                            cy=back_y,
                            cz=hole_bottom_z + USB_HEIGHT/2)
    meshes.append(back_right)
    
    # --- LEFT WALL with USB hole ---
    left_x = -OUTER_WIDTH/2 + WALL_THICKNESS/2
    
    left_bottom = create_box(WALL_THICKNESS, inner_depth, USB_HOLE_BOTTOM,
                             cx=left_x,
                             cz=WALL_THICKNESS + USB_HOLE_BOTTOM/2)
    meshes.append(left_bottom)
    
    if top_section_height > 0:
        left_top = create_box(WALL_THICKNESS, inner_depth, top_section_height,
                              cx=left_x,
                              cz=hole_top_z + top_section_height/2)
        meshes.append(left_top)
    
    side_hole_section = (inner_depth - USB_WIDTH) / 2
    left_front = create_box(WALL_THICKNESS, side_hole_section, USB_HEIGHT,
                            cx=left_x,
                            cy=-inner_depth/2 + side_hole_section/2,
                            cz=hole_bottom_z + USB_HEIGHT/2)
    meshes.append(left_front)
    
    left_back = create_box(WALL_THICKNESS, side_hole_section, USB_HEIGHT,
                           cx=left_x,
                           cy=inner_depth/2 - side_hole_section/2,
                           cz=hole_bottom_z + USB_HEIGHT/2)
    meshes.append(left_back)
    
    # --- RIGHT WALL with USB hole ---
    right_x = OUTER_WIDTH/2 - WALL_THICKNESS/2
    
    right_bottom = create_box(WALL_THICKNESS, inner_depth, USB_HOLE_BOTTOM,
                              cx=right_x,
                              cz=WALL_THICKNESS + USB_HOLE_BOTTOM/2)
    meshes.append(right_bottom)
    
    if top_section_height > 0:
        right_top = create_box(WALL_THICKNESS, inner_depth, top_section_height,
                               cx=right_x,
                               cz=hole_top_z + top_section_height/2)
        meshes.append(right_top)
    
    right_front = create_box(WALL_THICKNESS, side_hole_section, USB_HEIGHT,
                             cx=right_x,
                             cy=-inner_depth/2 + side_hole_section/2,
                             cz=hole_bottom_z + USB_HEIGHT/2)
    meshes.append(right_front)
    
    right_back = create_box(WALL_THICKNESS, side_hole_section, USB_HEIGHT,
                            cx=right_x,
                            cy=inner_depth/2 - side_hole_section/2,
                            cz=hole_bottom_z + USB_HEIGHT/2)
    meshes.append(right_back)
    
    # === INTERNAL LEDGE ===
    # Four strips around the inside perimeter for the stand to rest on
    # The ledge is inset from the walls and the stand (98x187) sits on top of it
    
    # Ledge sits LEDGE_FROM_TOP down from the top of the box
    # The opening above the ledge matches the stand size exactly
    
    # Front ledge strip
    front_ledge = create_box(inner_width, LEDGE_WIDTH, WALL_THICKNESS,
                             cy=-inner_depth/2 + LEDGE_WIDTH/2,
                             cz=ledge_z + WALL_THICKNESS/2)
    meshes.append(front_ledge)
    
    # Back ledge strip
    back_ledge = create_box(inner_width, LEDGE_WIDTH, WALL_THICKNESS,
                            cy=inner_depth/2 - LEDGE_WIDTH/2,
                            cz=ledge_z + WALL_THICKNESS/2)
    meshes.append(back_ledge)
    
    # Left ledge strip (between front and back ledges)
    ledge_inner_depth = inner_depth - 2 * LEDGE_WIDTH
    left_ledge = create_box(LEDGE_WIDTH, ledge_inner_depth, WALL_THICKNESS,
                            cx=-inner_width/2 + LEDGE_WIDTH/2,
                            cz=ledge_z + WALL_THICKNESS/2)
    meshes.append(left_ledge)
    
    # Right ledge strip
    right_ledge = create_box(LEDGE_WIDTH, ledge_inner_depth, WALL_THICKNESS,
                             cx=inner_width/2 - LEDGE_WIDTH/2,
                             cz=ledge_z + WALL_THICKNESS/2)
    meshes.append(right_ledge)
    
    # Combine all meshes
    combined = mesh.Mesh(np.concatenate([m.data for m in meshes]))
    return combined

if __name__ == '__main__':
    print(f"Creating cable tray (stand drops in from top)...")
    print(f"  Outer: {OUTER_WIDTH:.1f} x {OUTER_DEPTH:.1f} x {TRAY_HEIGHT:.1f} mm")
    print(f"  Stand opening: {STAND_WIDTH:.1f} x {STAND_DEPTH:.1f} mm")
    print(f"  Stand drops in: {LEDGE_FROM_TOP:.1f} mm from top")
    print(f"  Internal ledge width: {LEDGE_WIDTH:.1f} mm")
    print(f"  USB holes: {USB_WIDTH:.1f} x {USB_HEIGHT:.1f} mm on left, right, and back")
    
    tray = create_tray()
    tray.save('cable_tray.stl')
    print(f"Saved to cable_tray.stl")
